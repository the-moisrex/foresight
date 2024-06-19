cmake_minimum_required(VERSION 3.29)
project(foresight
        LANGUAGES CXX)

# For clang 16
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(fmt REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

add_executable(${PROJECT_NAME})
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
target_sources(${PROJECT_NAME}
        PRIVATE
            main.cxx
            keyboard.cxx
            provider.cxx
            evdev.cxx
        PUBLIC
            FILE_SET CXX_MODULES FILES
                keyboard.ixx
                provider.ixx
                evdev.ixx
                translate.ixx
)
target_link_libraries(${PROJECT_NAME}
        PRIVATE spdlog
        PRIVATE fmt
        PRIVATE Threads::Threads
        PRIVATE evdev
)
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${LIBEVDEV_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${LIBEVDEV_LIBRARIES})
target_compile_options(${PROJECT_NAME} PRIVATE ${LIBEVDEV_CFLAGS})
target_link_options(${PROJECT_NAME} PRIVATE ${LIBEVDEV_LDFLAGS})

