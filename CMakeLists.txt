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

# 2. Resolve OS-specific Magik binary paths
if(WIN32)
    set(MAGIK_LINK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../Magik/build/$<CONFIG>/magik.lib")
    set(MAGIK_BIN_FILE  "${CMAKE_CURRENT_SOURCE_DIR}/../Magik/build/$<CONFIG>/magik.dll")
else()
    # Linux: Links and runs against the same .so file. 
    set(MAGIK_LINK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../Magik/build/libmagik.so")
    set(MAGIK_BIN_FILE  "${MAGIK_LINK_FILE}")
endif()

# 3. Link against Magik
target_include_directories(magik_gui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../Magik/include/magik"
)

target_link_libraries(magik_gui PRIVATE
    "${MAGIK_LINK_FILE}"
)

# 4. Link libraries to the exact same target name
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

add_custom_command(TARGET magik_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${MAGIK_BIN_FILE}"
    "$<TARGET_FILE_DIR:magik_gui>"
)

if(WIN32)
    target_link_libraries(magik_gui PRIVATE opengl32)
else()
    target_link_libraries(magik_gui PRIVATE GL)
endif()