cmake_minimum_required(VERSION 4.0)

project(yamp)

add_subdirectory(vendor/glfw)
add_subdirectory(vendor/miniaudio)
add_subdirectory(vendor/portable-file-dialogs)
add_subdirectory(vendor/id3v2lib)
add_subdirectory(vendor/spdlog)
find_package(OpenGL REQUIRED)

add_executable(yamp
    ${CMAKE_HOME_DIRECTORY}/src/main.cpp
    ${CMAKE_HOME_DIRECTORY}/src/ui.cpp
    ${CMAKE_HOME_DIRECTORY}/src/mp.cpp
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui/imgui_demo.cpp
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui/imgui_widgets.cpp
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui/backends/imgui_impl_opengl3.cpp
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui/imgui_tables.cpp
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui/imgui.cpp
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui/imgui_draw.cpp
    ${CMAKE_HOME_DIRECTORY}/vendor/sqlite-amalgamation-3530400/sqlite3.c
)

target_include_directories(yamp
PRIVATE
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui 
    ${CMAKE_HOME_DIRECTORY}/vendor/imgui/backends
    ${CMAKE_HOME_DIRECTORY}/vendor/sqlite-amalgamation-3530400
)

target_link_libraries(yamp PRIVATE glfw OpenGL::GL portable_file_dialogs miniaudio id3v2lib spdlog)
