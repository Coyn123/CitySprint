cmake_minimum_required(VERSION 3.10)

# Set the project name
project(MyProject)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Add source files
add_executable(CitySprintServer main.cpp server.cpp)

# Link the Winsock library (ws2_32)
if(WIN32)
    target_link_libraries(CitySprintServer PRIVATE ws2_32)
else()
    target_link_libraries(CitySprintServer PRIVATE pthread)
endif()

# Set working directory for Visual Studio
set_property(TARGET CitySprintServer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")