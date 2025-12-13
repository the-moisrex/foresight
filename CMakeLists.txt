cmake_minimum_required(VERSION 3.23...4.1.2)
project(foresight LANGUAGES CXX C)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

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
#find_package(PkgConfig REQUIRED)
#pkg_check_modules(LIBEVDEV REQUIRED libevdev)
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
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
target_sources(${LIB_NAME}
        PRIVATE
        bash/bash_runner.cxx
        devices/evdev.cxx
        devices/key_codes.cxx
        devices/uinput.cxx
        lib/event2unicode.cxx
        lib/how2type.cxx
        lib/mod_parser.cxx
        lib/xkb.cxx
        main/keyboard.cxx
        main/systemd.cxx
        mods/abs2rel.cxx
        mods/emitter.cxx
        mods/ignore.cxx
        mods/intercept.cxx
        mods/keys_status.cxx
        mods/momentum.cxx
        mods/on.cxx
        mods/quantifier.cxx
        mods/replace.cxx
        mods/typed.cxx
        mods/typer.cxx
        utils/hash.cxx
        PUBLIC FILE_SET foresight TYPE CXX_MODULES FILES
        bash/bash_runner.ixx
        devices/evdev.ixx
        devices/inputs-event-codes.ixx
        devices/key_codes.ixx
        devices/uinput.ixx
        lib/event2unicode.ixx
        lib/how2type.ixx
        lib/mod_parser.ixx
        lib/xkb.ixx
        main/keyboard.ixx
        main/log.ixx
        main/main.ixx
        main/systemd.ixx
        main/translate.ixx
        main/utils.ixx
        mods/abs2rel.ixx
        mods/add_scroll.ixx
        mods/caps.ixx
        mods/context.ixx
        mods/context_vars.ixx
        mods/emitter.ixx
        mods/event.ixx
        mods/ignore.ixx
        mods/inout.ixx
        mods/intercept.ixx
        mods/keys_status.ixx
        mods/lambda.ixx
        mods/modes.ixx
        mods/mods.ixx
        mods/momentum.ixx
        mods/mouse_status.ixx
        mods/on.ixx
        mods/quantifier.ixx
        mods/replace.ixx
        mods/router.ixx
        mods/smooth.ixx
        mods/stopper.ixx
        mods/typed.ixx
        mods/typer.ixx
        mods/vars.ixx
        utils/easings.ixx
        utils/hash.ixx
        utils/strings.ixx
)
target_compile_features(${LIB_NAME} PUBLIC cxx_std_26)
set_target_properties(${LIB_NAME} PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
target_include_directories(${LIB_NAME} SYSTEM PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_compile_options(${LIB_NAME} PRIVATE ${LIBEVDEV_CFLAGS})
target_link_options(${LIB_NAME} PRIVATE ${LIBEVDEV_LDFLAGS})
target_link_libraries(${LIB_NAME}
        PUBLIC libevdev
        PUBLIC xkbcommon
)

install(TARGETS ${LIB_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)


#############################################################
########## -------- Foresight Executable --------- ##########
#############################################################

add_executable(${PROJECT_NAME})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_26)
target_sources(${PROJECT_NAME}
        PRIVATE
        main/main.cxx
)
target_link_libraries(${PROJECT_NAME}
        PRIVATE ${LIB_NAME}::${LIB_NAME}
        # PRIVATE Qt${QT_VERSION_MAJOR}::Core
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
