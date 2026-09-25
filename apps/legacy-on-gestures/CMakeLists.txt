set(name legacy-gesture)
add_executable(legacy-gesture)
target_sources(legacy-gesture PRIVATE on-gesture.cpp)
set_target_properties(legacy-gesture PROPERTIES OUTPUT_NAME legacy-gesture)
set_target_properties(legacy-gesture PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(legacy-gesture PROPERTIES COMPILER_LANGUAGE CXX)
set_target_properties(legacy-gesture PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

target_compile_features(legacy-gesture PUBLIC cxx_std_26)
set_target_properties(legacy-gesture PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
