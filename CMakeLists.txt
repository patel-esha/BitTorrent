cmake_minimum_required(VERSION 3.27)
project(bit_torrent)

set(CMAKE_CXX_STANDARD 17)

add_executable(bit_torrent main.cpp
        peer.cpp
        peer.h
)
