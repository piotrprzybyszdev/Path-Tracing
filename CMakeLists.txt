cmake_minimum_required(VERSION 3.25)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CONFIGURATION_TYPES Release Profile Debug Trace)

project(Path-Tracing LANGUAGES CXX C)

include(${CMAKE_SOURCE_DIR}/cmake/Utils.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/DownloadAssets.cmake)

add_configuration(TRACE DEBUG TRUE)
add_configuration(PROFILE RELEASE FALSE)

option(BUILD_SHARED_LIBS OFF)
option(ASSIMP_BUILD_TESTS OFF)
option(ASSIMP_INSTALL OFF)

add_subdirectory(Path-Tracing)
add_subdirectory(vendor/glfw)
add_subdirectory(vendor/imgui)
add_subdirectory(vendor/glm)
add_subdirectory(vendor/spdlog)
add_subdirectory(vendor/stb)
add_subdirectory(vendor/vma)
add_subdirectory(vendor/assimp)
add_subdirectory(vendor/nativefiledialog-extended)

set_target_properties(glfw PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(uninstall PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(update_mappings PROPERTIES FOLDER vendor/GLFW3)
set_target_properties(imgui PROPERTIES FOLDER vendor)
set_target_properties(glm PROPERTIES FOLDER vendor)
set_target_properties(spdlog PROPERTIES FOLDER vendor)
set_target_properties(stb PROPERTIES FOLDER vendor)
set_target_properties(vma PROPERTIES FOLDER vendor)
set_target_properties(assimp PROPERTIES FOLDER vendor/assimp)
set_target_properties(nfd PROPERTIES FOLDER vendor)

if (TARGET UpdateAssimpLibsDebugSymbolsAndDLLs)
    set_target_properties(UpdateAssimpLibsDebugSymbolsAndDLLs PROPERTIES FOLDER vendor/assimp)
endif()
if (TARGET zlibstatic)
    set_target_properties(zlibstatic PROPERTIES FOLDER vendor/assimp)
endif()

if (MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "Path-Tracing")
endif()

download_assets()

create_directory_link(${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/assets)
