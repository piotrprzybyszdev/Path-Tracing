find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)

set(SHADER_HEADER_FILES Shaders/ShaderTypes.incl Shaders/ShaderRendererTypes.incl Shaders/common.glsl)
set(SHADER_SOURCE_FILES Shaders/raygen.rgen Shaders/miss.rmiss Shaders/closesthit.rchit Shaders/anyhit.rahit)
set(SHADER_SPV_FILES)

if(NOT ${CMAKE_CURRENT_SOURCE_DIR} PATH_EQUAL ${CMAKE_CURRENT_BINARY_DIR})
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Shaders)
    foreach (SHADER_FILE IN LISTS SHADER_HEADER_FILES SHADER_SOURCE_FILES)
        file(CREATE_LINK ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE} ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_FILE})
    endforeach()
endif()

add_executable(Path-Tracing Core/Core.h Core/Core.cpp Core/Registry.h Core/Input.h Core/Input.cpp Core/Camera.h Core/Camera.cpp Renderer/Utils.h Renderer/DeviceContext.h Renderer/DeviceContext.cpp Renderer/Buffer.h Renderer/Buffer.cpp Renderer/Image.h Renderer/Image.cpp Renderer/DescriptorSet.h Renderer/DescriptorSet.cpp Renderer/AccelerationStructure.h Renderer/AccelerationStructure.cpp Renderer/ShaderBindingTable.h Renderer/ShaderBindingTable.cpp Renderer/ShaderLibrary.h Renderer/ShaderLibrary.cpp Renderer/Swapchain.h Renderer/Swapchain.cpp Renderer/Renderer.h Renderer/Renderer.cpp AssetManager.h AssetManager.cpp Scene.h Scene.cpp ExampleScenes.h ExampleScenes.cpp UserInterface.h UserInterface.cpp Window.h Window.cpp Application.h Application.cpp main.cpp)

target_sources(Path-Tracing PRIVATE ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})
source_group("Shaders" FILES ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})

target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/Path-Tracing)
target_link_libraries(Path-Tracing glfw imgui glm spdlog stb vma assimp Vulkan::Vulkan Vulkan::shaderc_combined)

if (CMAKE_GENERATOR MATCHES "^Visual Studio")
    target_link_options(Path-Tracing PRIVATE "/ignore:4099")
endif()
