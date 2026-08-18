cmake_minimum_required(VERSION 3.21)
project(magik_gui CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find vcpkg packages
find_package(glad CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

# 1. Define executable target name
add_executable(magik_gui src/main.cpp)

# 2. Link libraries to the exact same target name
target_link_libraries(magik_gui PRIVATE glad::glad glfw)

if(WIN32)
    target_link_libraries(magik_gui PRIVATE opengl32)
else()
    target_link_libraries(magik_gui PRIVATE GL)
endif()