cmake_minimum_required(VERSION 3.23)
project(foresight
        LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(fmt REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVDEV REQUIRED libevdev)

set(CMAKE_CXX_STANDARD 23)

add_executable(${PROJECT_NAME} main.cpp keyboard.cpp provider.cpp evdev.cpp)
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

