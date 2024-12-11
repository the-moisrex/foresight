cmake_minimum_required(VERSION 3.23...3.30)

project(foresight
        LANGUAGES CXX C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)


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
            main/main.cxx
            main/keyboard.cxx
            main/provider.cxx
            evdev/evdev.cxx
            uinput/uinput.cxx
            uinput/redirect.cxx
            intercept/intercept.cxx
        PUBLIC
            FILE_SET foresight TYPE CXX_MODULES
            BASE_DIRS
                ${PROJECT_SOURCE_DIR}
            FILES
                main/liburing.ixx
                main/keyboard.ixx
                main/provider.ixx
                main/translate.ixx
                evdev/evdev.ixx
                intercept/intercept.ixx
                uinput/redirect.ixx
                uinput/uinput.ixx
)

# Libraries
#find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
include(./cmake/import.cmake)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${LIBEVDEV_CFLAGS})
target_link_options(${PROJECT_NAME} PRIVATE ${LIBEVDEV_LDFLAGS})
target_link_libraries(${PROJECT_NAME}
        PRIVATE Threads::Threads
        PRIVATE fmt::fmt
#        PRIVATE spdlog::spdlog
        PRIVATE evdev
        PRIVATE Qt${QT_VERSION_MAJOR}::Core
        PRIVATE ${LIBEVDEV_LIBRARIES}
        PRIVATE liburing::liburing
)

if (IS_DEBUG)
    add_subdirectory(tests EXCLUDE_FROM_ALL)
    add_subdirectory(examples EXCLUDE_FROM_ALL)
endif ()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
