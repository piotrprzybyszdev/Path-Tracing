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

set(ASSETS textures/PavingStones textures/Metal textures/Logs scenes/KhronosScenes textures/skybox)
set(ASSET_URLS https://ambientcg.com/get?file=PavingStones142_1K-JPG.zip https://ambientcg.com/get?file=Metal062C_1K-JPG.zip https://ambientcg.com/get?file=Logs001_1K-JPG.zip https://github.com/KhronosGroup/glTF-Sample-Models/archive/refs/heads/main.zip https://drive.google.com/uc?id=1zg2hlXbV598pRHIVO8Qsu13OpGHLwuFc)
list(LENGTH ASSETS ASSET_COUNT)

set(INDEX 1)
foreach (NAME URL IN ZIP_LISTS ASSETS ASSET_URLS)
    set(ZIP_PATH "${CMAKE_SOURCE_DIR}/assets/${NAME}.zip")
    set(FOLDER_PATH "${CMAKE_SOURCE_DIR}/assets/${NAME}")

    if (NOT EXISTS ${FOLDER_PATH})
        message("Downloading asset: ${FOLDER_PATH} (${INDEX}/${ASSET_COUNT})")
        file(DOWNLOAD ${URL} ${ZIP_PATH})
        file(ARCHIVE_EXTRACT INPUT ${ZIP_PATH} DESTINATION ${FOLDER_PATH})
    else()
        message("Asset already exists: ${FOLDER_PATH} (${INDEX}/${ASSET_COUNT})")
    endif()

    math(EXPR INDEX "${INDEX} + 1")
endforeach()


get_filename_component(SOURCE_DIR ${CMAKE_SOURCE_DIR} ABSOLUTE)
get_filename_component(BINARY_DIR ${CMAKE_BINARY_DIR} ABSOLUTE)

if (NOT SOURCE_DIR STREQUAL BINARY_DIR)
    # Creating a symlink on Windows requires administrative permissions - create junction link instead
    if (WIN32)
        file(TO_NATIVE_PATH ${CMAKE_SOURCE_DIR}/assets WIN_SRC_ASSET)
        file(TO_NATIVE_PATH ${CMAKE_BINARY_DIR}/assets WIN_DST_ASSET)
        execute_process(COMMAND cmd.exe /c mklink /J ${WIN_DST_ASSET} ${WIN_SRC_ASSET} COMMAND_ERROR_IS_FATAL ANY)
    else()
        file(CREATE_LINK ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets SYMBOLIC)
    endif()
endif()
