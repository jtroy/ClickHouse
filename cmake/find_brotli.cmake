option (USE_INTERNAL_BROTLI_LIBRARY "Set to FALSE to use system brotli library instead of bundled" ${NOT_UNBUNDLED})

if (USE_INTERNAL_BROTLI_LIBRARY AND NOT EXISTS "${ClickHouse_SOURCE_DIR}/contrib/brotli/c/include/brotli/encode.h")
   message (WARNING "submodule contrib/brotli is missing. to fix try run: \n git submodule update --init --recursive")
   set (USE_INTERNAL_BROTLI_LIBRARY 0)
endif ()

if (NOT USE_INTERNAL_BROTLI_LIBRARY)
    find_library (BROTLI_LIBRARY brotli)
    find_path (BROTLI_INCLUDE_DIR NAMES encode.h PATHS ${BROTLI_INCLUDE_PATHS})
endif ()

if (BROTLI_LIBRARY AND BROTLI_INCLUDE_DIR)
else ()
    set (BROTLI_INCLUDE_DIR ${ClickHouse_SOURCE_DIR}/contrib/brotli/c/include)
    set (USE_INTERNAL_BROTLI_LIBRARY 1)
    set (BROTLI_LIBRARIES brotlienc brotlidec)
endif ()

message (STATUS "Using brotli: ${BROTLI_INCLUDE_DIR} : ${BROTLI_LIBRARIES}")
