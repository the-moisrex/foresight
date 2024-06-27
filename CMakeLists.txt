cmake_minimum_required(VERSION 3.23...3.29.6)

project(foresight
        LANGUAGES CXX)


if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(IS_DEBUG TRUE)
    message(STATUS "Debug mode is enabled.")
else ()
    set(IS_DEBUG FALSE)
    message(STATUS "Debug mode is disabled.")
endif ()


if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(IS_GCC TRUE)
    set(IS_CLANG FALSE)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(IS_GCC FALSE)
    set(IS_CLANG TRUE)
endif ()


if (IS_GCC AND IS_DEBUG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -fconcepts-diagnostics-depth=10 -ftemplate-backtrace-limit=0 -Wall -Wpedantic -Wextra -Wconversion -Wshadow")

    # For profiling (https://www.jetbrains.com/help/clion/cpu-profiler.html#UsingTheProfiler)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer")

elseif (IS_CLANG AND IS_DEBUG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wpedantic -Wextra -Wconversion -Wshadow")

    # For profiling (https://www.jetbrains.com/help/clion/cpu-profiler.html#UsingTheProfiler)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer")

endif ()


if (IS_CLANG)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif ()


add_executable(${PROJECT_NAME})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
target_sources(${PROJECT_NAME}
        PRIVATE
            main.cxx
            keyboard.cxx
            provider.cxx
            evdev/evdev.cxx
            intercept/intercept.cxx
        PUBLIC
            FILE_SET foresight TYPE CXX_MODULES
            BASE_DIRS
                ${PROJECT_SOURCE_DIR}
            FILES
                keyboard.ixx
                provider.ixx
                evdev/evdev.ixx
                translate.ixx
                intercept/intercept.ixx
                redirect/redirect.ixx
)

# Libraries
find_package(Threads REQUIRED)
find_package(fmt REQUIRED)
#find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBEVDEV_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE ${LIBEVDEV_CFLAGS})
target_link_options(${PROJECT_NAME} PRIVATE ${LIBEVDEV_LDFLAGS})
target_link_libraries(${PROJECT_NAME}
        PRIVATE Threads::Threads
        PRIVATE fmt::fmt
#        PRIVATE spdlog::spdlog
        PRIVATE evdev
)
