set(name legacy-long-press)
add_executable(legacy-long-press)
target_sources(legacy-long-press PRIVATE long-press.cpp)
set_target_properties(legacy-long-press PROPERTIES OUTPUT_NAME legacy-long-press)
set_target_properties(legacy-long-press PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(legacy-long-press PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(legacy-long-press PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_compile_features(legacy-long-press PUBLIC cxx_std_26)
set_target_properties(legacy-long-press PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
