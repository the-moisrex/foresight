set(name benchmarks)

# Shared header directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/common)

# Copy event files to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/events DESTINATION ${CMAKE_BINARY_DIR}/benchmarks)

# Micro-benchmarks (Google Benchmark)
file(GLOB MICRO_BENCH_SOURCES micro/*_bench.cxx)
foreach(source ${MICRO_BENCH_SOURCES})
    get_filename_component(identifier "${source}" NAME)
    string(REPLACE "_bench.cxx" "" identifier "${identifier}")
    string(REPLACE "_" "-" identifier "${identifier}")
    set(bench_name "bench-${identifier}")

    add_executable(${bench_name})
    target_sources(${bench_name} PRIVATE ${source})
    target_link_libraries(${bench_name}
        PRIVATE foresight::foresight
        PRIVATE benchmark::benchmark
        PRIVATE benchmark::benchmark_main
    )
    set_target_properties(${bench_name} PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    message(STATUS "Micro-benchmark available: ${bench_name}")
endforeach()

# Pipeline benchmarks (using benchmark.ixx mod)
file(GLOB PIPELINE_BENCH_SOURCES pipeline/*_bench.cxx)
foreach(source ${PIPELINE_BENCH_SOURCES})
    get_filename_component(identifier "${source}" NAME)
    string(REPLACE "_bench.cxx" "" identifier "${identifier}")
    string(REPLACE "_" "-" identifier "${identifier}")
    set(bench_name "bench-${identifier}")

    add_executable(${bench_name})
    target_sources(${bench_name} PRIVATE ${source})
    target_link_libraries(${bench_name}
        PRIVATE foresight::foresight
    )
    set_target_properties(${bench_name} PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    message(STATUS "Pipeline benchmark available: ${bench_name}")
endforeach()
