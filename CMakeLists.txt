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

set(ASSETS PavingStones Metal Logs)
set(ASSET_URLS https://ambientcg.com/get?file=PavingStones142_1K-JPG.zip https://ambientcg.com/get?file=Metal062C_1K-JPG.zip https://ambientcg.com/get?file=Logs001_1K-JPG.zip)
list(LENGTH ASSETS ASSET_COUNT)

set(INDEX 1)
foreach(NAME URL IN ZIP_LISTS ASSETS ASSET_URLS)
    set(ZIP_PATH "${CMAKE_BINARY_DIR}/assets/${NAME}.zip")
    set(FOLDER_PATH "${CMAKE_BINARY_DIR}/assets/${NAME}")

    if (NOT EXISTS ${FOLDER_PATH})
        message("Downloading asset: ${FOLDER_PATH} (${INDEX}/${ASSET_COUNT})")
        file(DOWNLOAD ${URL} ${ZIP_PATH})
        file(ARCHIVE_EXTRACT INPUT ${ZIP_PATH} DESTINATION ${FOLDER_PATH})
    else()
        message("Asset already exists: ${FOLDER_PATH} (${INDEX}/${ASSET_COUNT})")
    endif()

    math(EXPR INDEX "${INDEX} + 1")
endforeach()
