SET(LIBRARY_DIR ${ClickHouse_SOURCE_DIR}/contrib/brotli/c)

add_library(brotlienc
    ${LIBRARY_DIR}/common/dictionary.c
    ${LIBRARY_DIR}/common/transform.c

    ${LIBRARY_DIR}/common/constants.h
    ${LIBRARY_DIR}/common/context.h
    ${LIBRARY_DIR}/common/dictionary.h
    ${LIBRARY_DIR}/common/platform.h
    ${LIBRARY_DIR}/common/transform.h
    ${LIBRARY_DIR}/common/version.h

    ${LIBRARY_DIR}/enc/backward_references.c
    ${LIBRARY_DIR}/enc/backward_references_hq.c
    ${LIBRARY_DIR}/enc/bit_cost.c
    ${LIBRARY_DIR}/enc/block_splitter.c
    ${LIBRARY_DIR}/enc/brotli_bit_stream.c
    ${LIBRARY_DIR}/enc/cluster.c
    ${LIBRARY_DIR}/enc/compress_fragment.c
    ${LIBRARY_DIR}/enc/compress_fragment_two_pass.c
    ${LIBRARY_DIR}/enc/dictionary_hash.c
    ${LIBRARY_DIR}/enc/encode.c
    ${LIBRARY_DIR}/enc/encoder_dict.c
    ${LIBRARY_DIR}/enc/entropy_encode.c
    ${LIBRARY_DIR}/enc/histogram.c
    ${LIBRARY_DIR}/enc/literal_cost.c
    ${LIBRARY_DIR}/enc/memory.c
    ${LIBRARY_DIR}/enc/metablock.c
    ${LIBRARY_DIR}/enc/static_dict.c
    ${LIBRARY_DIR}/enc/utf8_util.c

    ${LIBRARY_DIR}/enc/backward_references.h
    ${LIBRARY_DIR}/enc/backward_references_hq.h
    ${LIBRARY_DIR}/enc/backward_references_inc.h
    ${LIBRARY_DIR}/enc/bit_cost.h
    ${LIBRARY_DIR}/enc/bit_cost_inc.h
    ${LIBRARY_DIR}/enc/block_encoder_inc.h
    ${LIBRARY_DIR}/enc/block_splitter.h
    ${LIBRARY_DIR}/enc/block_splitter_inc.h
    ${LIBRARY_DIR}/enc/brotli_bit_stream.h
    ${LIBRARY_DIR}/enc/cluster.h
    ${LIBRARY_DIR}/enc/cluster_inc.h
    ${LIBRARY_DIR}/enc/command.h
    ${LIBRARY_DIR}/enc/compress_fragment.h
    ${LIBRARY_DIR}/enc/compress_fragment_two_pass.h
    ${LIBRARY_DIR}/enc/dictionary_hash.h
    ${LIBRARY_DIR}/enc/encoder_dict.h
    ${LIBRARY_DIR}/enc/entropy_encode.h
    ${LIBRARY_DIR}/enc/entropy_encode_static.h
    ${LIBRARY_DIR}/enc/fast_log.h
    ${LIBRARY_DIR}/enc/find_match_length.h
    ${LIBRARY_DIR}/enc/hash.h
    ${LIBRARY_DIR}/enc/hash_forgetful_chain_inc.h
    ${LIBRARY_DIR}/enc/hash_longest_match64_inc.h
    ${LIBRARY_DIR}/enc/hash_longest_match_inc.h
    ${LIBRARY_DIR}/enc/hash_longest_match_quickly_inc.h
    ${LIBRARY_DIR}/enc/hash_to_binary_tree_inc.h
    ${LIBRARY_DIR}/enc/histogram.h
    ${LIBRARY_DIR}/enc/histogram_inc.h
    ${LIBRARY_DIR}/enc/literal_cost.h
    ${LIBRARY_DIR}/enc/memory.h
    ${LIBRARY_DIR}/enc/metablock.h
    ${LIBRARY_DIR}/enc/metablock_inc.h
    ${LIBRARY_DIR}/enc/params.h
    ${LIBRARY_DIR}/enc/prefix.h
    ${LIBRARY_DIR}/enc/quality.h
    ${LIBRARY_DIR}/enc/ringbuffer.h
    ${LIBRARY_DIR}/enc/static_dict.h
    ${LIBRARY_DIR}/enc/static_dict_lut.h
    ${LIBRARY_DIR}/enc/utf8_util.h
    ${LIBRARY_DIR}/enc/write_bits.h
    )

add_library(brotlidec
    ${LIBRARY_DIR}/common/dictionary.c
    ${LIBRARY_DIR}/common/transform.c

    ${LIBRARY_DIR}/common/constants.h
    ${LIBRARY_DIR}/common/context.h
    ${LIBRARY_DIR}/common/dictionary.h
    ${LIBRARY_DIR}/common/platform.h
    ${LIBRARY_DIR}/common/transform.h
    ${LIBRARY_DIR}/common/version.h

    ${LIBRARY_DIR}/dec/bit_reader.c
    ${LIBRARY_DIR}/dec/decode.c
    ${LIBRARY_DIR}/dec/huffman.c
    ${LIBRARY_DIR}/dec/state.c

    ${LIBRARY_DIR}/dec/bit_reader.h
    ${LIBRARY_DIR}/dec/huffman.h
    ${LIBRARY_DIR}/dec/prefix.h
    ${LIBRARY_DIR}/dec/state.h
    )

target_include_directories (brotlienc PUBLIC ${LIBRARY_DIR}/include)
target_include_directories (brotlidec PUBLIC ${LIBRARY_DIR}/include)
