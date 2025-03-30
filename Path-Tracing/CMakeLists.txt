add_executable(Path-Tracing Core/Core.h Core/Core.cpp Core/Input.h Core/Input.cpp Core/Camera.h Core/Camera.cpp Renderer/PhysicalDevice.h Renderer/PhysicalDevice.cpp Renderer/LogicalDevice.h Renderer/LogicalDevice.cpp Renderer/Buffer.h Renderer/Buffer.cpp Renderer/Image.h Renderer/Image.cpp Renderer/Frame.h Renderer/Frame.cpp Renderer/ShaderLibrary.h Renderer/ShaderLibrary.cpp Renderer/Renderer.h Renderer/Renderer.cpp Window.h Window.cpp main.cpp)

find_package(Vulkan REQUIRED)

target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/Path-Tracing)
target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui/backends)
target_include_directories(Path-Tracing PUBLIC Vulkan::Vulkan)
target_link_libraries(Path-Tracing glfw imgui glm spdlog Vulkan::Vulkan)