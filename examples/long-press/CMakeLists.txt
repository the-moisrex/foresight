set(name long-press)
add_executable(long-press)
target_sources(long-press PRIVATE long-press.cpp)
set_target_properties(long-press PROPERTIES OUTPUT_NAME long-press)
set_target_properties(long-press PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(long-press PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(long-press PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_compile_features(long-press PUBLIC cxx_std_23)
set_target_properties(long-press PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
