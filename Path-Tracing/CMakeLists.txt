find_package(Vulkan REQUIRED)

set(SHADER_SOURCE_FILES Shaders/raygen.rgen Shaders/closesthit.rchit Shaders/miss.rmiss)
set(SHADER_SPV_FILES)

foreach(SHADER_SOURCE IN LISTS SHADER_SOURCE_FILES)
    cmake_path(REPLACE_EXTENSION SHADER_SOURCE "spv" OUTPUT_VARIABLE SHADER_SPV)
    list(APPEND SHADER_SPV_FILES ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_SPV})

    add_custom_command(OUTPUT ${SHADER_SPV}
                       COMMAND ${CMAKE_COMMAND} -E make_directory Shaders
                       COMMAND Vulkan::glslc --target-env=vulkan1.4 -o ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_SPV} ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}
                       DEPENDS ${SHADER_SOURCE} VERBATIM)
endforeach()

add_executable(Path-Tracing Core/Core.h Core/Core.cpp Core/Input.h Core/Input.cpp Core/Camera.h Core/Camera.cpp Renderer/DeviceContext.h Renderer/DeviceContext.cpp Renderer/Buffer.h Renderer/Buffer.cpp Renderer/Image.h Renderer/Image.cpp Renderer/DescriptorSet.h Renderer/DescriptorSet.cpp Renderer/AccelerationStructure.h Renderer/AccelerationStructure.cpp Renderer/ShaderLibrary.h Renderer/ShaderLibrary.cpp Renderer/Swapchain.h Renderer/Swapchain.cpp Renderer/Renderer.h Renderer/Renderer.cpp MaterialSystem.h MaterialSystem.cpp UserInterface.h UserInterface.cpp Window.h Window.cpp Application.h Application.cpp main.cpp)

set(SHADER_HEADER_FILES Shaders/ShaderTypes.incl)

target_sources(Path-Tracing PRIVATE ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})
target_sources(Path-Tracing PRIVATE ${SHADER_SPV_FILES})
source_group("Shaders" FILES ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})
source_group("Compiled Shaders" FILES ${SHADER_SPV_FILES})

target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/Path-Tracing)
target_include_directories(Path-Tracing PRIVATE Vulkan::Vulkan)
target_link_libraries(Path-Tracing glfw imgui glm spdlog stb vma Vulkan::Vulkan)
