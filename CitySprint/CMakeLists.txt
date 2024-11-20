cmake_minimum_required(VERSION 3.10)
project(WebSocketGame)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Create our executable
add_executable(CitySprint "game_server.cpp" "misc_lib.cpp")

if (WIN32)
    set(CMAKE_SYSTEM_NAME Windows)
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
    set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
    target_link_libraries(CitySprint ${CMAKE_SOURCE_DIR}/lib/libssl.lib ${CMAKE_SOURCE_DIR}/lib/libcrypto.lib ws2_32)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(CitySprint OpenSSL::SSL OpenSSL::Crypto pthread)
endif()