include(${CMAKE_SOURCE_DIR}/cmake/ConfigureVulkan.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Utils.cmake)

set(SHADER_INCLUDE_FILES Shaders/TestShaderTypes.incl)
set(SHADER_SOURCE_FILES Shaders/testPadding.comp Shaders/testShading.comp)

set(HEADER_FILES TestRenderer.h TestEnvironment.h)
set(SOURCE_FILES main.cpp PaddingTest.cpp ShadingTest.cpp TestRenderer.cpp TestEnvironment.cpp TestApplication.cpp)
set(APPLICATION_SOURCE_FILES ../Path-Tracing/Core/Core.cpp ../Path-Tracing/Core/Config.cpp ../Path-Tracing/Renderer/CommandBuffer.cpp ../Path-Tracing/Renderer/Pipeline.cpp ../Path-Tracing/Renderer/ShaderLibrary.cpp ../Path-Tracing/Renderer/DeviceContext.cpp ../Path-Tracing/Renderer/DescriptorSet.cpp ../Path-Tracing/Renderer/Image.cpp ../Path-Tracing/Renderer/Buffer.cpp)

create_directory_link(${CMAKE_SOURCE_DIR}/Path-Tracing/Shaders ${CMAKE_CURRENT_SOURCE_DIR}/Shaders/Application)
create_directory_link(${CMAKE_CURRENT_SOURCE_DIR}/Shaders ${CMAKE_CURRENT_BINARY_DIR}/Shaders)

add_executable(Path-Tracing-Tests ${HEADER_FILES} ${SOURCE_FILES} ${APPLICATION_SOURCE_FILES} ${SHADER_INCLUDE_FILES} ${SHADER_SOURCE_FILES})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders PREFIX Shaders/Includes FILES ${SHADER_INCLUDE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/Shaders PREFIX Shaders/Sources FILES ${SHADER_SOURCE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Header Files" FILES ${HEADER_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCE_FILES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/../Path-Tracing PREFIX "Application/Source Files" FILES ${APPLICATION_SOURCE_FILES})

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

