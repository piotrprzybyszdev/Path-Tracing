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
add_subdirectory(vendor/vma)
add_subdirectory(vendor/assimp)

set_target_properties(glfw PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(uninstall PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(update_mappings PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(imgui PROPERTIES FOLDER vendor)
set_target_properties(glm PROPERTIES FOLDER vendor)
set_target_properties(spdlog PROPERTIES FOLDER vendor)
set_target_properties(stb PROPERTIES FOLDER vendor)
set_target_properties(vma PROPERTIES FOLDER vendor)
set_target_properties(assimp PROPERTIES FOLDER vendor/assimp)
set_target_properties(unit PROPERTIES FOLDER vendor/assimp)

if (CMAKE_GENERATOR MATCHES "^Visual Studio")
    set_target_properties(UpdateAssimpLibsDebugSymbolsAndDLLs PROPERTIES FOLDER vendor/assimp)
    set_target_properties(zlibstatic PROPERTIES FOLDER vendor/assimp)
endif()

if (MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "Path-Tracing")
endif()

include(${CMAKE_SOURCE_DIR}/cmake/Utils.cmake)

set(ASSETS textures/PavingStones textures/Metal textures/Logs scenes/KhronosScenes textures/skybox)
set(ASSET_URLS https://ambientcg.com/get?file=PavingStones142_1K-JPG.zip https://ambientcg.com/get?file=Metal062C_1K-JPG.zip https://ambientcg.com/get?file=Logs001_1K-JPG.zip https://github.com/KhronosGroup/glTF-Sample-Models/archive/refs/heads/main.zip https://drive.google.com/uc?id=1zg2hlXbV598pRHIVO8Qsu13OpGHLwuFc)

download_assets("${ASSETS}" "${ASSET_URLS}")

create_directory_link(${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets)
