add_library(vma VulkanMemoryAllocator/include/vk_mem_alloc.h implementation.cpp)

find_package(Vulkan REQUIRED)

target_include_directories(vma PUBLIC ${CMAKE_SOURCE_DIR}/vendor/vma/VulkanMemoryAllocator/include)
target_link_libraries(vma PUBLIC Vulkan::Vulkan)