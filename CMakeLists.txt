cmake_minimum_required(VERSION 3.21)
project(magik_gui CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find vcpkg packages
find_package(glad CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(nfd CONFIG REQUIRED)

# 1. Define executable target name
add_executable(magik_gui src/main.cpp src/stb_image.cpp)

# 2. Link libraries to the exact same target name
target_link_libraries(magik_gui PRIVATE 
    glad::glad 
    glfw 
    imgui::imgui
    nfd::nfd
)

target_include_directories(magik_gui PRIVATE ${Stb_INCLUDE_DIR})

add_custom_command(TARGET magik_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:magik_gui>/assets"
)

if(WIN32)
    target_link_libraries(magik_gui PRIVATE opengl32)
else()
    target_link_libraries(magik_gui PRIVATE GL)
endif()