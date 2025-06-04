set(name scroll)
add_executable(scroll)
target_sources(scroll PRIVATE scroll.cpp)
set_target_properties(scroll PROPERTIES OUTPUT_NAME scroll)
set_target_properties(scroll PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(scroll PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(scroll PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

