enable_testing()
include(GoogleTest)

include(../cmake/coverage.cmake)

function(setup_test name)
    if (TARGET gtest AND TARGET gtest_main)
        target_link_libraries(${name}
                PRIVATE gtest         # GTest::GTest
                PRIVATE gtest_main    # GTest::Main
        )
    elseif (TARGET GTest::GTest AND TARGET GTest::Main)
        target_link_libraries(${name}
                PRIVATE GTest::GTest
                PRIVATE GTest::Main
        )
    endif ()
    target_link_libraries(${name}
            PUBLIC ${PROJECT_NAME}::${PROJECT_NAME}
    )
    add_test(
            NAME ${name}
            COMMAND $<TARGET_FILE:${name}>)
    set_target_properties(${name} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    target_compile_features(${name} PUBLIC cxx_std_26)
    set_target_properties(${name} PROPERTIES
            CXX_STANDARD 26
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS ON)
    target_code_coverage("${name}")
    gtest_discover_tests(${name})

    message(STATUS "Test available: ${name}")
endfunction()



# All the tests in one single executable
file(GLOB TEST_SOURCES *_test.cxx)
set(TEST_NAME ${PROJECT_NAME}-tests)
add_executable(${TEST_NAME})
target_sources(${TEST_NAME} PRIVATE ${TEST_SOURCES})
set_target_properties(${EXEC_NAME} PROPERTIES OUTPUT_NAME tests)
setup_test(${TEST_NAME})



# Each file is a test-suite of its own
# I did this because we might have compile-time errors for the newly added tests
foreach(source ${TEST_SOURCES})
    get_filename_component(identifier "${source}" NAME)
    string(REPLACE "_test.cxx" "" identifier "${identifier}")
    string(REPLACE "_" "-" identifier "${identifier}")
    set(name "test-${identifier}")

    add_executable(${name})
    target_sources(${name} PRIVATE ${source})

    setup_test(${name})
endforeach()


