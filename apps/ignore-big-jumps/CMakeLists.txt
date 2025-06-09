set(name ignore-big-jumps)
add_executable(ignore-big-jumps)
target_sources(ignore-big-jumps PRIVATE ignore-big-jumps.cpp)
set_target_properties(ignore-big-jumps PROPERTIES OUTPUT_NAME ignore-big-jumps)
set_target_properties(ignore-big-jumps PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(ignore-big-jumps PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(ignore-big-jumps PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_compile_features(ignore-big-jumps PUBLIC cxx_std_23)
set_target_properties(ignore-big-jumps PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
