cmake_minimum_required(VERSION 3.16)
project(beseda LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(beseda src/main.cpp src/chat.cpp src/udp_socket.cpp)
target_compile_options(beseda PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(beseda PRIVATE Threads::Threads)
