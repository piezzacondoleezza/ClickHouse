set (CLICKHOUSE_AVX_SIP_TESTING_SOURCES AvxSipTesting.cpp)

set (CLICKHOUSE_AVX_SIP_TESTING_LINK
    PRIVATE
        boost::program_options
        clickhouse_common_io
)

set_source_files_properties(${ClickHouse_SOURCE_DIR}/programs/avx-sip-testing/AvxSipTesting.cpp PROPERTIES COMPILE_FLAGS "-mssse3")
set_source_files_properties(${ClickHouse_SOURCE_DIR}/programs/avx-sip-testing/AvxSipTesting.cpp PROPERTIES COMPILE_FLAGS "-msse4.1")
set_source_files_properties(${ClickHouse_SOURCE_DIR}/programs/avx-sip-testing/AvxSipTesting.cpp PROPERTIES COMPILE_FLAGS "-msse4.2")
set_source_files_properties(${ClickHouse_SOURCE_DIR}/programs/avx-sip-testing/AvxSipTesting.cpp PROPERTIES COMPILE_FLAGS "-mavx")
set_source_files_properties(${ClickHouse_SOURCE_DIR}/programs/avx-sip-testing/AvxSipTesting.cpp PROPERTIES COMPILE_FLAGS "-mavx2")


clickhouse_program_add(avx-sip-testing)
