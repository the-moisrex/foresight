cmake_minimum_required(VERSION 3.23...4.0)

project(foresight LANGUAGES CXX C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
# set(CMAKE_AUTOUIC ON)
# set(CMAKE_AUTOMOC ON)
# set(CMAKE_AUTORCC ON)


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
include(./cmake/import.cmake)
include(GNUInstallDirs)

# Libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)
#find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
#find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Widgets REQUIRED)


#############################################################
########## ---------- Foresight Library ---------- ##########
#############################################################

set(LIB_NAME ${PROJECT_NAME}_lib)
add_library(${LIB_NAME})
add_library(${LIB_NAME}::${LIB_NAME} ALIAS ${LIB_NAME})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${LIB_NAME})

set_target_properties(${LIB_NAME} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
target_sources(${LIB_NAME}
        PRIVATE
        main/keyboard.cxx
        evdev/evdev.cxx
        uinput/uinput.cxx
        uinput/redirect.cxx
        mods/intercept.cxx
        PUBLIC FILE_SET foresight TYPE CXX_MODULES FILES
        # io/liburing.ixx
        # io/io_ring.ixx
        main/main.ixx
        main/keyboard.ixx
        main/translate.ixx
        evdev/evdev.ixx
        uinput/redirect.ixx
        uinput/uinput.ixx
        mods/event.ixx
        mods/intercept.ixx
        mods/stopper.ixx
        mods/keys_status.ixx
        mods/quantifier.ixx
        mods/context.ixx
        mods/inout.ixx
        mods/rel_jumps.ixx
        mods/add_scroll.ixx
        mods/mods.ixx
)
target_compile_features(${LIB_NAME} PUBLIC cxx_std_23)
set_target_properties(${LIB_NAME} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
target_include_directories(${LIB_NAME} SYSTEM PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_compile_options(${LIB_NAME} PRIVATE ${LIBEVDEV_CFLAGS})
target_link_options(${LIB_NAME} PRIVATE ${LIBEVDEV_LDFLAGS})
target_link_libraries(${LIB_NAME}
        PRIVATE evdev
        PRIVATE ${LIBEVDEV_LIBRARIES}
        # PRIVATE Threads::Threads
)

install(TARGETS ${LIB_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


#############################################################
########## -------- Foresight Executable --------- ##########
#############################################################

add_executable(${PROJECT_NAME})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
target_sources(${PROJECT_NAME}
        PRIVATE
        main/main.cxx
)

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_compile_options(${PROJECT_NAME} PRIVATE ${LIBEVDEV_CFLAGS})
target_link_options(${PROJECT_NAME} PRIVATE ${LIBEVDEV_LDFLAGS})
target_link_libraries(${PROJECT_NAME}
        PRIVATE ${LIB_NAME}::${LIB_NAME}
        #        PRIVATE Qt${QT_VERSION_MAJOR}::Core
)

install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

#############################################################

if (IS_DEBUG)
    add_subdirectory(tests)
endif ()
add_subdirectory(apps)
