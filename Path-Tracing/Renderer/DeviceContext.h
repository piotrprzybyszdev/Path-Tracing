#pragma once

#include <vector>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace PathTracing
{

class DeviceContext
{
public:
    static void Init(
        vk::Instance instance, uint32_t vulkanApiVersion, const std::vector<const char *> &requestedLayers,
        vk::SurfaceKHR surface
    );
    static void Shutdown();

    static vk::PhysicalDevice GetPhysical();
    static vk::Device GetLogical();

    static std::vector<uint32_t> GetQueueFamilyIndices();
    static uint32_t GetGraphicsQueueFamilyIndex();

    static vk::Queue GetPresentQueue();
    static vk::Queue GetGraphicsQueue();

    static VmaAllocator GetAllocator();

private:
    static struct PhysicalDevice
    {
        vk::PhysicalDevice Handle { nullptr };

        vk::PhysicalDeviceProperties Properties;
        std::vector<vk::QueueFamilyProperties> QueueFamilyProperties;
        vk::PhysicalDeviceMemoryProperties MemoryProperties;
    } s_PhysicalDevice;

    static struct LogicalDevice
    {
        vk::Device Handle { nullptr };

        // Supports present, graphics and compute
        uint32_t MainQueueFamilyIndex = vk::QueueFamilyIgnored;
        vk::Queue MainQueue;
    } s_LogicalDevice;

    static VmaAllocator s_Allocator;

private:
    static bool CheckSuitable(
        vk::PhysicalDevice device, const std::vector<const char *> &requestedExtensions,
        const std::vector<const char *> &requestedLayers
    );
};

}
