cmake_minimum_required(VERSION 3.23...4.1.2)

# Read version from VERSION file
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FORESIGHT_VERSION)
string(STRIP "${FORESIGHT_VERSION}" FORESIGHT_VERSION)

project(foresight VERSION ${FORESIGHT_VERSION} LANGUAGES CXX C)
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
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wpedantic -Wextra -Wconversion -Wshadow")

    # For profiling (https://www.jetbrains.com/help/clion/cpu-profiler.html#UsingTheProfiler)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer")

    # AddressSanitizer for catching use-after-free / double-free at runtime.
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")

elseif (IS_CLANG AND IS_DEBUG)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -Wall -Wpedantic -Wextra -Wconversion -Wshadow")

    # For profiling (https://www.jetbrains.com/help/clion/cpu-profiler.html#UsingTheProfiler)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer")

    # AddressSanitizer for catching use-after-free / double-free at runtime.
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address")

endif ()


if (IS_CLANG)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif ()
include(./cmake/import.cmake)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
        devices/capabilities.cxx
        devices/evdev.cxx
        devices/key_codes.cxx
        devices/queries.cxx
        devices/udev.cxx
        devices/uinput.cxx
        lib/event2unicode.cxx
        lib/evtest.cxx
        lib/how2type.cxx
        lib/mod_parser.cxx
        lib/xkb.cxx
        main/context.cxx
        main/event.cxx
        main/keyboard.cxx
        main/new.cxx
        main/systemd.cxx
        mods/abs2rel.cxx
        mods/autocomplete.cxx
        mods/benchmark.cxx
        mods/device.cxx
        mods/emitter.cxx
        mods/drop.cxx
        mods/inout.cxx
        mods/live_view.cxx
        mods/input_manager.cxx
        mods/intercept.cxx
        mods/io_manager.cxx
        mods/keys_state.cxx
        mods/momentum.cxx
        mods/on.cxx
        mods/quantifier.cxx
        mods/record.cxx
        mods/sanitizer.cxx
        mods/replace.cxx
        mods/router.cxx
        mods/scheduler.cxx
        mods/scale.cxx
        mods/singleton.cxx
        mods/smooth.cxx
        mods/timed_typed.cxx
        mods/typed.cxx
        mods/typer.cxx
        utils/hash.cxx
        PUBLIC FILE_SET foresight TYPE CXX_MODULES FILES
        bash/bash_runner.ixx
        devices/capabilities.ixx
        devices/evdev.ixx
        devices/inputs-event-codes.ixx
        devices/key_codes.ixx
        devices/queries.ixx
        devices/udev.ixx
        devices/uinput.ixx
        lib/event2unicode.ixx
        lib/evtest.ixx
        lib/how2type.ixx
        lib/mod_parser.ixx
        lib/xkb.ixx
        main/cli.ixx
        main/context.ixx
        main/event.ixx
        main/keyboard.ixx
        main/log.ixx
        main/main.ixx
        main/new.ixx
        main/systemd.ixx
        main/translate.ixx
        main/utils.ixx
        mods/abs2rel.ixx
        mods/autocomplete.ixx
        mods/benchmark.ixx
        mods/context_vars.ixx
        mods/debounce.ixx
        mods/device.ixx
        mods/emitter.ixx
        mods/drop.ixx
        mods/inout.ixx
        mods/live_view.ixx
        mods/input_manager.ixx
        mods/intercept.ixx
        mods/io_manager.ixx
        mods/keys_state.ixx
        mods/lambda.ixx
        mods/modes.ixx
        mods/mods.ixx
        mods/momentum.ixx
        mods/mouse_state.ixx
        mods/mouse_to_scroll.ixx
        mods/on.ixx
        mods/quantifier.ixx
        mods/record.ixx
        mods/sanitizer.ixx
        mods/replace.ixx
        mods/router.ixx
        mods/scheduler.ixx
        mods/scale.ixx
        mods/singleton.ixx
        mods/smooth.ixx
        mods/stopper.ixx
        mods/timed_typed.ixx
        mods/typed.ixx
        mods/typer.ixx
        mods/vars.ixx
        utils/dynamic_scoping.ixx
        utils/easings.ixx
        utils/hash.ixx
        utils/nullable_indirect.ixx
        utils/pimpl.ixx
        utils/strings.ixx
        utils/traits.ixx
)

target_compile_features(${LIB_NAME} PUBLIC cxx_std_26)
set_target_properties(${LIB_NAME} PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON)
target_include_directories(${LIB_NAME} SYSTEM PRIVATE ${LIBEVDEV_INCLUDE_DIRS})
target_compile_options(${LIB_NAME} PRIVATE ${LIBEVDEV_CFLAGS})
target_link_options(${LIB_NAME} PRIVATE ${LIBEVDEV_LDFLAGS})
target_compile_definitions(${LIB_NAME}
        PUBLIC FORESIGHT_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(${LIB_NAME}
        PUBLIC libevdev
        PUBLIC udev
        PUBLIC xkbcommon
)

install(TARGETS ${LIB_NAME}
        EXPORT ${LIB_NAME}-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILE_SET foresight DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
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
########## --------- CMake Package Config ---------- ##########
#############################################################

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/ForesightConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ForesightConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/ForesightConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/foresight
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ForesightConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ForesightConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/foresight
)

install(EXPORT ${LIB_NAME}-targets
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/foresight
)

#############################################################

if (IS_DEBUG)
    add_subdirectory(tests)
endif ()
add_subdirectory(apps)


#############################################################
########## --------------- Docs Target ----------- ##########
#############################################################

find_package(Doxygen)
if (Doxygen_FOUND)
    add_custom_target(docs
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/docs/doxygen"
            COMMAND ${CMAKE_COMMAND} -E env
            "FS8_DOC_OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/docs/doxygen"
            ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Generating the Foresight API reference with Doxygen."
            VERBATIM)
endif ()
