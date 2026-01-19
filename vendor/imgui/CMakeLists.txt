add_library(imgui STATIC imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_vulkan.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_demo.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp)

find_package(Vulkan REQUIRED)

target_include_directories(imgui PUBLIC ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui/backends)
target_include_directories(imgui PUBLIC ${CMAKE_SOURCE_DIR}/vendor/imgui/imgui)
target_include_directories(imgui PUBLIC ${CMAKE_SOURCE_DIR}/vendor/glfw/include)

target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
