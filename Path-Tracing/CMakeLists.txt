add_executable(Path-Tracing Core/Core.h Core/Core.cpp main.cpp)

target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/Path-Tracing)
target_include_directories(Path-Tracing PRIVATE ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui/backends)
target_link_libraries(Path-Tracing glfw imgui glm spdlog)