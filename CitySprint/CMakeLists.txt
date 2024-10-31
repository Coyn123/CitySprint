cmake_minimum_required(VERSION 3.10)
project(WebSocketGame)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

#Create our executable
add_executable(WebSocketGame "game_server.cpp")

# Link libraries
target_link_libraries(WebSocketGame "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MDd/libssl.lib" "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MDd/libcrypto.lib")
target_link_libraries(WebSocketGame ws2_32)