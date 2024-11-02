set(name x2y)
add_executable(x2y)
target_sources(x2y PRIVATE x2y.c)
set_target_properties(x2y PROPERTIES OUTPUT_NAME x2y)
set_target_properties(x2y PROPERTIES LINKER_LANGUAGE C)
set_target_properties(x2y PROPERTIES COMPILER_LANGUAGE C)
set_target_properties(x2y PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

