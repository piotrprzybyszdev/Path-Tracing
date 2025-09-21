find_package(Vulkan REQUIRED COMPONENTS shaderc_combined)

function (create_directory_link SRC DST)
    get_filename_component(DST_PATH ${DST} ABSOLUTE)
    if (IS_DIRECTORY ${DST_PATH})
        return()
    endif()

    # Creating a symlink on Windows requires administrative permissions - create junction link instead
    if (WIN32)
        file(TO_NATIVE_PATH ${SRC} WIN_SRC_ASSET)
        file(TO_NATIVE_PATH ${DST} WIN_DST_ASSET)
        execute_process(COMMAND cmd.exe /c mklink /J ${WIN_DST_ASSET} ${WIN_SRC_ASSET})
    else()
        file(CREATE_LINK ${SRC} ${DST} SYMBOLIC)
    endif()
endfunction()

set(SHADER_HEADER_FILES Shaders/ShaderTypes.incl Shaders/ShaderRendererTypes.incl Shaders/common.glsl)
set(SHADER_SOURCE_FILES Shaders/raygen.rgen Shaders/miss.rmiss Shaders/closesthit.rchit Shaders/anyhit.rahit Shaders/occlusion.rmiss Shaders/skinning.comp)
set(SHADER_SPV_FILES)

create_directory_link(${CMAKE_CURRENT_SOURCE_DIR}/Shaders ${CMAKE_CURRENT_BINARY_DIR}/Shaders)

add_executable(Path-Tracing Core/Core.h Core/Core.cpp Core/Input.h Core/Input.cpp Core/Camera.h Core/Camera.cpp Renderer/Utils.h Renderer/DeviceContext.h Renderer/DeviceContext.cpp Renderer/Buffer.h Renderer/Buffer.cpp Renderer/Image.h Renderer/Image.cpp Renderer/DescriptorSet.h Renderer/DescriptorSet.cpp Renderer/AccelerationStructure.h Renderer/AccelerationStructure.cpp Renderer/ShaderBindingTable.h Renderer/ShaderBindingTable.cpp Renderer/ShaderLibrary.h Renderer/ShaderLibrary.cpp Renderer/CommandBuffer.h Renderer/CommandBuffer.cpp Renderer/TextureUploader.h Renderer/TextureUploader.cpp Renderer/Swapchain.h Renderer/Swapchain.cpp Renderer/Renderer.h Renderer/Renderer.cpp AssetImporter.h AssetImporter.cpp SceneGraph.h SceneGraph.cpp Scene.h Scene.cpp SceneManager.h SceneManager.cpp ExampleScenes.h ExampleScenes.cpp UserInterface.h UserInterface.cpp Window.h Window.cpp Application.h Application.cpp main.cpp)

target_sources(Path-Tracing PRIVATE ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})
source_group("Shaders" FILES ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})

target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/Path-Tracing)
target_link_libraries(Path-Tracing glfw imgui glm spdlog stb vma assimp Vulkan::Vulkan Vulkan::shaderc_combined)

if (CMAKE_GENERATOR MATCHES "^Visual Studio")
    target_link_options(Path-Tracing PRIVATE "/ignore:4099")
endif()

if(MSVC)
    target_compile_options(Path-Tracing PRIVATE "/MP")
endif()