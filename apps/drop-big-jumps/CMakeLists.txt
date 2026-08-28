set(name drop-big-jumps)
add_executable(drop-big-jumps)
target_sources(drop-big-jumps PRIVATE drop-big-jumps.cpp)
set_target_properties(drop-big-jumps PROPERTIES OUTPUT_NAME drop-big-jumps)
set_target_properties(drop-big-jumps PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(drop-big-jumps PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(drop-big-jumps PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_compile_features(drop-big-jumps PUBLIC cxx_std_26)
set_target_properties(drop-big-jumps PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
install(TARGETS ${name}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
