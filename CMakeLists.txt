cmake_minimum_required(VERSION 3.27)
project(bit_torrent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

add_executable(bit_torrent main.cpp peer.cpp peer.h)
target_link_libraries(client Threads::Threads)
