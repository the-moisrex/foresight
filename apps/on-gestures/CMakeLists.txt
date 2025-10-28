set(name gesture)
add_executable(gesture)
target_sources(gesture PRIVATE on-gesture.cpp)
set_target_properties(gesture PROPERTIES OUTPUT_NAME gesture)
set_target_properties(gesture PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(gesture PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(gesture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_compile_features(gesture PUBLIC cxx_std_26)
set_target_properties(gesture PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
