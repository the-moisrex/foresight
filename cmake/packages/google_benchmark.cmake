include(CPM)
CPMAddPackage(
        NAME benchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.5.tar.gz
        OPTIONS "BENCHMARK_ENABLE_TESTING OFF"
)

if (benchmark_ADDED)
    # patch benchmark target
    set_target_properties(benchmark PROPERTIES CXX_STANDARD 26)
    # Suppress warnings that conflict with our -Werror flags
    set(BENCHMARK_WARNING_FLAGS
        -Wno-conversion
        -Wno-float-conversion
        -Wno-old-style-cast
        -Wno-c2y-extensions
    )
    target_compile_options(benchmark PRIVATE ${BENCHMARK_WARNING_FLAGS})
    if (TARGET benchmark_main)
        target_compile_options(benchmark_main PRIVATE ${BENCHMARK_WARNING_FLAGS})
    endif ()
endif ()
