#include <IO/Operators.h>
#include <Storages/StorageReplicatedMergeTree.h>
#include <Storages/MergeTree/ReplicatedMergeTreeRestartingThread.h>
#include <Storages/MergeTree/ReplicatedMergeTreeQuorumEntry.h>
#include <Storages/MergeTree/ReplicatedMergeTreeAddress.h>
#include <Common/setThreadName.h>
#include <Common/ZooKeeper/KeeperException.h>
#include <Common/randomSeed.h>


namespace ProfileEvents
{
    extern const Event ReplicaYieldLeadership;
    extern const Event ReplicaPartialShutdown;
}

namespace CurrentMetrics
{
    extern const Metric ReadonlyReplica;
}


namespace DB
{

namespace ErrorCodes
{
    extern const int REPLICA_IS_ALREADY_ACTIVE;
}


/// Used to check whether it's us who set node `is_active`, or not.
static String generateActiveNodeIdentifier()
{
    return "pid: " + toString(getpid()) + ", random: " + toString(randomSeed());
}


ReplicatedMergeTreeRestartingThread::ReplicatedMergeTreeRestartingThread(StorageReplicatedMergeTree & storage_)
    : storage(storage_),
    log(&Logger::get(storage.database_name + "." + storage.table_name + " (StorageReplicatedMergeTree, RestartingThread)")),
    active_node_identifier(generateActiveNodeIdentifier()),
    thread([this] { run(); })
{
}


void ReplicatedMergeTreeRestartingThread::run()
{
    constexpr auto retry_period_ms = 10 * 1000;

    /// The frequency of checking expiration of session in ZK.
    Int64 check_period_ms = storage.data.settings.zookeeper_session_expiration_check_period.totalSeconds() * 1000;

    /// Periodicity of checking lag of replica.
    if (check_period_ms > static_cast<Int64>(storage.data.settings.check_delay_period) * 1000)
        check_period_ms = storage.data.settings.check_delay_period * 1000;

    setThreadName("ReplMTRestart");

    bool first_time = true;                 /// Activate replica for the first time.
    time_t prev_time_of_check_delay = 0;

    /// Starts the replica when the server starts/creates a table. Restart the replica when session expires with ZK.
    while (!need_stop)
    {
        try
        {
            if (first_time || storage.getZooKeeper()->expired())
            {
                if (first_time)
                {
                    LOG_DEBUG(log, "Activating replica.");
                }
                else
                {
                    LOG_WARNING(log, "ZooKeeper session has expired. Switching to a new session.");

                    bool old_val = false;
                    if (storage.is_readonly.compare_exchange_strong(old_val, true))
                        CurrentMetrics::add(CurrentMetrics::ReadonlyReplica);

                    partialShutdown();
                }

                while (!need_stop)
                {
                    try
                    {
                        storage.setZooKeeper(storage.context.getZooKeeper());
                    }
                    catch (const zkutil::KeeperException & e)
                    {
                        /// The exception when you try to zookeeper_init usually happens if DNS does not work. We will try to do it again.
                        tryLogCurrentException(log, __PRETTY_FUNCTION__);

                        if (first_time)
                            storage.startup_event.set();
                        wakeup_event.tryWait(retry_period_ms);
                        continue;
                    }

                    if (!need_stop && !tryStartup())
                    {
                        if (first_time)
                            storage.startup_event.set();
                        wakeup_event.tryWait(retry_period_ms);
                        continue;
                    }

                    if (first_time)
                        storage.startup_event.set();
                    break;
                }

                if (need_stop)
                    break;

                bool old_val = true;
                if (storage.is_readonly.compare_exchange_strong(old_val, false))
                    CurrentMetrics::sub(CurrentMetrics::ReadonlyReplica);

                first_time = false;
            }

            time_t current_time = time(nullptr);
            if (current_time >= prev_time_of_check_delay + static_cast<time_t>(storage.data.settings.check_delay_period))
            {
                /// Find out lag of replicas.
                time_t absolute_delay = 0;
                time_t relative_delay = 0;

                storage.getReplicaDelays(absolute_delay, relative_delay);

                if (absolute_delay)
                    LOG_TRACE(log, "Absolute delay: " << absolute_delay << ". Relative delay: " << relative_delay << ".");

                prev_time_of_check_delay = current_time;

                /// We give up leadership if the relative lag is greater than threshold.
                if (storage.is_leader
                    && relative_delay > static_cast<time_t>(storage.data.settings.min_relative_delay_to_yield_leadership))
                {
                    LOG_INFO(log, "Relative replica delay (" << relative_delay << " seconds) is bigger than threshold ("
                        << storage.data.settings.min_relative_delay_to_yield_leadership << "). Will yield leadership.");

                    ProfileEvents::increment(ProfileEvents::ReplicaYieldLeadership);

                    storage.exitLeaderElection();
                    /// NOTE: enterLeaderElection() can throw if node creation in ZK fails.
                    /// This is bad because we can end up without a leader on any replica.
                    /// In this case we rely on the fact that the session will expire and we will reconnect.
                    storage.enterLeaderElection();
                }
            }
        }
        catch (...)
        {
            storage.startup_event.set();
            tryLogCurrentException(log, __PRETTY_FUNCTION__);
        }

        wakeup_event.tryWait(check_period_ms);
    }

    try
    {
        storage.data_parts_exchange_endpoint_holder->cancelForever();
        storage.data_parts_exchange_endpoint_holder = nullptr;

        /// Cancel fetches and merges to force the queue_task to finish ASAP.
        storage.fetcher.blocker.cancelForever();
        storage.merger.merges_blocker.cancelForever();

        partialShutdown();

        if (storage.queue_task_handle)
            storage.context.getBackgroundPool().removeTask(storage.queue_task_handle);
        storage.queue_task_handle.reset();
    }
    catch (...)
    {
        tryLogCurrentException(log, __PRETTY_FUNCTION__);
    }

    LOG_DEBUG(log, "Restarting thread finished");
}


bool ReplicatedMergeTreeRestartingThread::tryStartup()
{
    try
    {
        removeFailedQuorumParts();
        activateReplica();
        updateQuorumIfWeHavePart();

        if (storage.data.settings.replicated_can_become_leader)
            storage.enterLeaderElection();

        /// Anything above can throw a KeeperException if something is wrong with ZK.
        /// Anything below should not throw exceptions.

        storage.shutdown_called = false;
        storage.shutdown_event.reset();

        storage.queue_updating_thread = std::thread(&StorageReplicatedMergeTree::queueUpdatingThread, &storage);
        storage.mutations_updating_thread = std::thread(&StorageReplicatedMergeTree::mutationsUpdatingThread, &storage);
        storage.part_check_thread.start();
        storage.alter_thread = std::make_unique<ReplicatedMergeTreeAlterThread>(storage);
        storage.cleanup_thread = std::make_unique<ReplicatedMergeTreeCleanupThread>(storage);

        if (!storage.queue_task_handle)
            storage.queue_task_handle = storage.context.getBackgroundPool().addTask(
                std::bind(&StorageReplicatedMergeTree::queueTask, &storage));

        return true;
    }
    catch (...)
    {
        storage.replica_is_active_node  = nullptr;

        try
        {
            throw;
        }
        catch (const zkutil::KeeperException & e)
        {
            LOG_ERROR(log, "Couldn't start replication: " << e.what() << ", " << e.displayText() << ", stack trace:\n" << e.getStackTrace().toString());
            return false;
        }
        catch (const Exception & e)
        {
            if (e.code() != ErrorCodes::REPLICA_IS_ALREADY_ACTIVE)
                throw;

            LOG_ERROR(log, "Couldn't start replication: " << e.what() << ", " << e.displayText() << ", stack trace:\n" << e.getStackTrace().toString());
            return false;
        }
    }
}


void ReplicatedMergeTreeRestartingThread::removeFailedQuorumParts()
{
    auto zookeeper = storage.getZooKeeper();

    Strings failed_parts;
    if (!zookeeper->tryGetChildren(storage.zookeeper_path + "/quorum/failed_parts", failed_parts))
        return;

    for (auto part_name : failed_parts)
    {
        auto part = storage.data.getPartIfExists(
            part_name, {MergeTreeDataPartState::PreCommitted, MergeTreeDataPartState::Committed, MergeTreeDataPartState::Outdated});
        if (part)
        {
            LOG_DEBUG(log, "Found part " << part_name << " with failed quorum. Moving to detached. This shouldn't happen often.");

            zkutil::Requests ops;
            zkutil::Responses responses;
            storage.removePartFromZooKeeper(part_name, ops);
            auto code = zookeeper->tryMulti(ops, responses);
            if (code == ZooKeeperImpl::ZooKeeper::ZNONODE)
                LOG_WARNING(log, "Part " << part_name << " with failed quorum is not in ZooKeeper. This shouldn't happen often.");

            storage.data.renameAndDetachPart(part, "noquorum");
        }
    }
}


void ReplicatedMergeTreeRestartingThread::updateQuorumIfWeHavePart()
{
    auto zookeeper = storage.getZooKeeper();

    String quorum_str;
    if (zookeeper->tryGet(storage.zookeeper_path + "/quorum/status", quorum_str))
    {
        ReplicatedMergeTreeQuorumEntry quorum_entry;
        quorum_entry.fromString(quorum_str);

        if (!quorum_entry.replicas.count(storage.replica_name)
            && zookeeper->exists(storage.replica_path + "/parts/" + quorum_entry.part_name))
        {
            LOG_WARNING(log, "We have part " << quorum_entry.part_name
                << " but we is not in quorum. Updating quorum. This shouldn't happen often.");
            storage.updateQuorum(quorum_entry.part_name);
        }
    }
}


void ReplicatedMergeTreeRestartingThread::activateReplica()
{
    auto host_port = storage.context.getInterserverIOAddress();
    auto zookeeper = storage.getZooKeeper();

    /// How other replicas can access this.
    ReplicatedMergeTreeAddress address;
    address.host = host_port.first;
    address.replication_port = host_port.second;
    address.queries_port = storage.context.getTCPPort();
    address.database = storage.database_name;
    address.table = storage.table_name;

    String is_active_path = storage.replica_path + "/is_active";

    /** If the node is marked as active, but the mark is made in the same instance, delete it.
      * This is possible only when session in ZooKeeper expires.
      */
    String data;
    zkutil::Stat stat;
    bool has_is_active = zookeeper->tryGet(is_active_path, data, &stat);
    if (has_is_active && data == active_node_identifier)
    {
        auto code = zookeeper->tryRemove(is_active_path, stat.version);

        if (code == ZooKeeperImpl::ZooKeeper::ZBADVERSION)
            throw Exception("Another instance of replica " + storage.replica_path + " was created just now."
                " You shouldn't run multiple instances of same replica. You need to check configuration files.",
                ErrorCodes::REPLICA_IS_ALREADY_ACTIVE);

        if (code && code != ZooKeeperImpl::ZooKeeper::ZNONODE)
            throw zkutil::KeeperException(code, is_active_path);
    }

    /// Simultaneously declare that this replica is active, and update the host.
    zkutil::Requests ops;
    ops.emplace_back(zkutil::makeCreateRequest(is_active_path, active_node_identifier, zkutil::CreateMode::Ephemeral));
    ops.emplace_back(zkutil::makeSetRequest(storage.replica_path + "/host", address.toString(), -1));

    try
    {
        zookeeper->multi(ops);
    }
    catch (const zkutil::KeeperException & e)
    {
        if (e.code == ZooKeeperImpl::ZooKeeper::ZNODEEXISTS)
            throw Exception("Replica " + storage.replica_path + " appears to be already active. If you're sure it's not, "
                "try again in a minute or remove znode " + storage.replica_path + "/is_active manually", ErrorCodes::REPLICA_IS_ALREADY_ACTIVE);

        throw;
    }

    /// `current_zookeeper` lives for the lifetime of `replica_is_active_node`,
    ///  since before changing `current_zookeeper`, `replica_is_active_node` object is destroyed in `partialShutdown` method.
    storage.replica_is_active_node = zkutil::EphemeralNodeHolder::existing(is_active_path, *storage.current_zookeeper);
}


void ReplicatedMergeTreeRestartingThread::partialShutdown()
{
    ProfileEvents::increment(ProfileEvents::ReplicaPartialShutdown);

    storage.shutdown_called = true;
    storage.shutdown_event.set();
    storage.merge_selecting_event.set();
    storage.queue_updating_event->set();
    storage.mutations_updating_event->set();
    storage.alter_query_event->set();
    storage.cleanup_thread_event.set();
    storage.replica_is_active_node = nullptr;

    LOG_TRACE(log, "Waiting for threads to finish");

    storage.exitLeaderElection();

    if (storage.queue_updating_thread.joinable())
        storage.queue_updating_thread.join();

    if (storage.mutations_updating_thread.joinable())
        storage.mutations_updating_thread.join();

    storage.cleanup_thread.reset();
    storage.alter_thread.reset();
    storage.part_check_thread.stop();

    LOG_TRACE(log, "Threads finished");
}

}
