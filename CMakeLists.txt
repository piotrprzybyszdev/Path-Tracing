cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(Path-Tracing LANGUAGES CXX C)

add_subdirectory(Path-Tracing)
add_subdirectory(vendor/glfw)
add_subdirectory(vendor/imgui)
add_subdirectory(vendor/glm)
add_subdirectory(vendor/spdlog)
add_subdirectory(vendor/stb)

set_target_properties(glfw PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(uninstall PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(update_mappings PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(imgui PROPERTIES FOLDER vendor)
set_target_properties(glm PROPERTIES FOLDER vendor)
set_target_properties(spdlog PROPERTIES FOLDER vendor)
set_target_properties(stb PROPERTIES FOLDER vendor)
