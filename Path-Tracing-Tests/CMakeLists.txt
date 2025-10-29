include(${CMAKE_SOURCE_DIR}/cmake/ConfigureVulkan.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Utils.cmake)

set(SHADER_HEADER_FILES Shaders/TestShaderTypes.incl)
set(SHADER_SOURCE_FILES Shaders/testPadding.comp Shaders/testShading.comp)

create_directory_link(${CMAKE_SOURCE_DIR}/Path-Tracing/Shaders ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Application)
create_directory_link(${CMAKE_CURRENT_SOURCE_DIR}/Shaders ${CMAKE_CURRENT_BINARY_DIR}/Shaders)

add_executable(Path-Tracing-Tests main.cpp PaddingTest.cpp ShadingTest.cpp TestRenderer.h TestRenderer.cpp TestEnvironment.h TestEnvironment.cpp TestApplication.cpp ../Path-Tracing/Core/Core.cpp ../Path-Tracing/Core/Config.cpp ../Path-Tracing/Renderer/CommandBuffer.cpp ../Path-Tracing/Renderer/Pipeline.cpp ../Path-Tracing/Renderer/ShaderLibrary.cpp ../Path-Tracing/Renderer/DeviceContext.cpp ../Path-Tracing/Renderer/DescriptorSet.cpp ../Path-Tracing/Renderer/Image.cpp ../Path-Tracing/Renderer/Buffer.cpp)

target_sources(Path-Tracing-Tests PRIVATE ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})
source_group("Shaders" FILES ${SHADER_SOURCE_FILES} ${SHADER_HEADER_FILES})

target_include_directories(Path-Tracing-Tests PRIVATE ${CMAKE_SOURCE_DIR}/Path-Tracing)
target_include_directories(Path-Tracing-Tests PRIVATE ${CMAKE_SOURCE_DIR}/vendor/googletest/googletest/include)
target_link_libraries(Path-Tracing-Tests gtest_main glm spdlog vma vulkan shaderc_combined spirv-cross-core)

if (CMAKE_GENERATOR MATCHES "^Visual Studio")
    target_link_options(Path-Tracing-Tests PRIVATE "/ignore:4099")
endif()

if(MSVC)
    target_compile_options(Path-Tracing-Tests PRIVATE "/MP")
endif()

target_compile_definitions(Path-Tracing-Tests PRIVATE PATH_TRACING_TESTS CONFIG_VALIDATION_LAYERS)

