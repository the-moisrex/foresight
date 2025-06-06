set(name scroll)
add_executable(scroll)
target_sources(scroll PRIVATE scroll.cpp)
set_target_properties(scroll PROPERTIES OUTPUT_NAME scroll)
set_target_properties(scroll PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(scroll PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(scroll PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_compile_features(scroll PUBLIC cxx_std_23)
set_target_properties(scroll PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
