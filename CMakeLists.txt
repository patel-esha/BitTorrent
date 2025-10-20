cmake_minimum_required(VERSION 3.27)
project(bit_torrent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

# server executable and link with libraries
add_executable(server server.cpp)
target_link_libraries(server Threads::Threads)

# client executable and link with libraries
add_executable(client client.cpp)
target_link_libraries(client Threads::Threads)
