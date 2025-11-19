include(${CMAKE_SOURCE_DIR}/cmake/ConfigureVulkan.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Utils.cmake)

set(SHADER_HEADER_FILES Shaders/ShaderTypes.incl Shaders/ShaderRendererTypes.incl Shaders/Debug/DebugShaderTypes.incl Shaders/common.glsl Shaders/shading.glsl Shaders/tracing.glsl Shaders/ray.glsl Shaders/sampling.glsl)
set(SHADER_SOURCE_FILES Shaders/raygen.rgen Shaders/Debug/debugRaygen.rgen Shaders/miss.rmiss Shaders/Debug/debugMiss.rmiss Shaders/metalicRoughness.rchit Shaders/specularGlossiness.rchit Shaders/Debug/debugClosestHit.rchit Shaders/anyhit.rahit Shaders/occlusion.rmiss Shaders/skinning.comp)

create_directory_link(${CMAKE_CURRENT_SOURCE_DIR}/Shaders ${CMAKE_CURRENT_BINARY_DIR}/Shaders)

add_executable(Path-Tracing Core/Config.h Core/Config.cpp Core/Core.h Core/Core.cpp Core/Cache.h Core/Threads.h Core/Input.h Core/Input.cpp Core/Camera.h Core/Camera.cpp Renderer/Utils.h Renderer/DeviceContext.h Renderer/DeviceContext.cpp Renderer/Buffer.h Renderer/Buffer.cpp Renderer/Image.h Renderer/Image.cpp Renderer/DescriptorSet.h Renderer/DescriptorSet.cpp Renderer/AccelerationStructure.h Renderer/AccelerationStructure.cpp Renderer/ShaderBindingTable.h Renderer/ShaderBindingTable.cpp Renderer/ShaderLibrary.h Renderer/ShaderLibrary.cpp Renderer/Pipeline.h Renderer/Pipeline.cpp Renderer/CommandBuffer.h Renderer/CommandBuffer.cpp Renderer/StagingBuffer.h Renderer/StagingBuffer.cpp Renderer/TextureUploader.h Renderer/TextureUploader.cpp Renderer/Swapchain.h Renderer/Swapchain.cpp Renderer/Renderer.h Renderer/Renderer.cpp TextureImporter.h TextureImporter.cpp SceneImporter.h SceneImporter.cpp SceneGraph.h SceneGraph.cpp Scene.h Scene.cpp SceneManager.h SceneManager.cpp ExampleScenes.h ExampleScenes.cpp Resources.h Resources.cpp UserInterface.h UserInterface.cpp UIComponents.h UIComponents.cpp Window.h Window.cpp Application.h Application.cpp main.cpp)

target_sources(Path-Tracing PRIVATE ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})
source_group("Shaders" FILES ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})

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
