
if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    # First, try to find the system-installed version
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(PC_LIBEVDEV QUIET libevdev)
    endif()

    if (PC_LIBEVDEV_FOUND)
        # Use the system-installed version
        message(STATUS "Found system libevdev: ${PC_LIBEVDEV_LINK_LIBRARIES}")
        add_library(libevdev STATIC IMPORTED)
        set_target_properties(libevdev PROPERTIES
            IMPORTED_LOCATION "${PC_LIBEVDEV_LINK_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${PC_LIBEVDEV_INCLUDE_DIRS}"
            INTERFACE_COMPILE_OPTIONS "${PC_LIBEVDEV_CFLAGS_OTHER}"
        )
    else()
        # System version not found, use CPM to download and build
        message(STATUS "System libevdev not found. Will download and build from source.")
        include(CPM)
        include(ExternalProject)

        # Use CPM to download the source only
        CPMAddPackage(
            NAME libevdev
            URL https://www.freedesktop.org/software/libevdev/libevdev-1.11.0.tar.xz
            VERSION 1.11.0
            DOWNLOAD_ONLY YES
        )

        if (libevdev_ADDED)
            # Define the external project build
            set(LIBEVDEV_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/libevdev-install)
            set(LIBEVDEV_INCLUDE_DIR ${LIBEVDEV_INSTALL_DIR}/include)
            set(LIBEVDEV_LIBRARY ${LIBEVDEV_INSTALL_DIR}/lib/libevdev.a) # Or .so if you prefer shared

            # Create a script to set the include directories after installation
            set(LIBEVDEV_CONFIG_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/libevdev-config.cmake)
            file(WRITE ${LIBEVDEV_CONFIG_SCRIPT}
                "set_target_properties(libevdev PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"${LIBEVDEV_INCLUDE_DIR}\")\n"
            )

            ExternalProject_Add(libevdev_ext
                SOURCE_DIR ${libevdev_SOURCE_DIR}
                BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libevdev-build
                CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${LIBEVDEV_INSTALL_DIR} --enable-shared=no --enable-static=yes
                BUILD_COMMAND ${MAKE}
                INSTALL_COMMAND ${MAKE} install
                # Run the script to set include directories after install
                COMMAND ${CMAKE_COMMAND} -P ${LIBEVDEV_CONFIG_SCRIPT}
                BUILD_BYPRODUCTS ${LIBEVDEV_LIBRARY}
            )

            # Create an imported target for libevdev
            add_library(libevdev STATIC IMPORTED)
            set_target_properties(libevdev PROPERTIES
                IMPORTED_LOCATION ${LIBEVDEV_LIBRARY}
            )

            # Ensure the external project is built before anything tries to link libevdev
            add_dependencies(libevdev libevdev_ext)
        endif()
    endif()
endif()
