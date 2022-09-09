cmake_minimum_required(VERSION 3.23)
project(foresight
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)

add_executable(foresight main.cpp eventio.cpp provider.cpp)
target_link_libraries(foresight
        PRIVATE spdlog
        PRIVATE thread
        )
