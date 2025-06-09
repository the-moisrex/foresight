set(name pen2mice)
add_executable(${name})
target_sources(${name} PRIVATE pen2mice.cpp)
set_target_properties(${name} PROPERTIES OUTPUT_NAME ${name})
set_target_properties(${name} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${name} PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_link_libraries(${name} PRIVATE foresight::foresight)
target_compile_features(${name} PUBLIC cxx_std_23)
set_target_properties(${name} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
