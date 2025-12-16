include(${CMAKE_SOURCE_DIR}/cmake/ConfigureVulkan.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Utils.cmake)

set(SHADER_INCLUDE_FILES Shaders/ShaderTypes.incl Shaders/ShaderRendererTypes.incl Shaders/Debug/DebugShaderTypes.incl)
set(SHADER_HEADER_FILES Shaders/common.glsl Shaders/shading.glsl Shaders/tracing.glsl Shaders/ray.glsl Shaders/sampling.glsl)
set(SHADER_SOURCE_FILES Shaders/raygen.rgen Shaders/Debug/debugRaygen.rgen Shaders/miss.rmiss Shaders/Debug/debugMiss.rmiss Shaders/metalicRoughness.rchit Shaders/specularGlossiness.rchit Shaders/Debug/debugClosestHit.rchit Shaders/anyhit.rahit Shaders/occlusion.rmiss Shaders/postprocess.comp Shaders/skinning.comp)

set(HEADER_FILES Core/Config.h Core/Core.h Core/Cache.h Core/Threads.h Core/Input.h Core/Camera.h Renderer/Utils.h Renderer/DeviceContext.h Renderer/Buffer.h Renderer/Image.h Renderer/DescriptorSet.h Renderer/AccelerationStructure.h Renderer/ShaderBindingTable.h Renderer/ShaderLibrary.h Renderer/Pipeline.h Renderer/CommandBuffer.h Renderer/StagingBuffer.h Renderer/TextureUploader.h Renderer/Swapchain.h Renderer/Renderer.h  TextureImporter.h SceneImporter.h SceneGraph.h Scene.h SceneManager.h ExampleScenes.h Resources.h UserInterface.h UIComponents.h Window.h Application.h)

set(SOURCE_FILES Core/Config.cpp Core/Core.cpp Core/Input.cpp Core/Camera.cpp Renderer/DeviceContext.cpp Renderer/Buffer.cpp Renderer/Image.cpp Renderer/DescriptorSet.cpp Renderer/AccelerationStructure.cpp Renderer/ShaderBindingTable.cpp Renderer/ShaderLibrary.cpp Renderer/Pipeline.cpp Renderer/CommandBuffer.cpp Renderer/StagingBuffer.cpp Renderer/TextureUploader.cpp Renderer/Swapchain.cpp Renderer/Renderer.cpp TextureImporter.cpp SceneImporter.cpp SceneGraph.cpp Scene.cpp SceneManager.cpp ExampleScenes.cpp Resources.cpp UserInterface.cpp Window.cpp Application.cpp main.cpp)

create_directory_link(${CMAKE_CURRENT_SOURCE_DIR}/Shaders ${CMAKE_CURRENT_BINARY_DIR}/Shaders)

add_executable(Path-Tracing ${HEADER_FILES} ${SOURCE_FILES} ${SHADER_INCLUDE_FILES} ${SHADER_HEADER_FILES} ${SHADER_SOURCE_FILES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADER_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders PREFIX Shaders/Includes FILES ${SHADER_INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders PREFIX Shaders/Headers FILES ${SHADER_HEADER_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders PREFIX Shaders/Sources FILES ${SHADER_SOURCE_FILES})

target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/vendor/gli)
target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/Path-Tracing)
target_link_libraries(Path-Tracing glfw imgui glm spdlog stb vma assimp nfd vulkan shaderc_combined spirv-cross-core)

if (CMAKE_GENERATOR MATCHES "^Visual Studio")
    target_link_options(Path-Tracing PRIVATE "/ignore:4099")
endif()

if(MSVC)
    target_compile_options(Path-Tracing PRIVATE "/MP")
endif()

target_compile_definitions(Path-Tracing PRIVATE "${PATH_TRACING_CONFIG}")

target_compile_definitions(Path-Tracing PRIVATE
    $<$<CONFIG:Release>:CONFIG_RELEASE>
    $<$<CONFIG:Profile>:CONFIG_PROFILE>
    $<$<CONFIG:Debug>:CONFIG_DEBUG>
    $<$<CONFIG:Trace>:CONFIG_TRACE>
)
