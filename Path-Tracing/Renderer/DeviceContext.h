#pragma once

#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace PathTracing
{

class DeviceContext
{
public:
    static void Init(
        vk::Instance instance, const std::vector<const char *> &requestedLayers, vk::SurfaceKHR surface
    );
    static void Shutdown();

    static vk::PhysicalDevice GetPhysical();
    static vk::Device GetLogical();

    static std::vector<uint32_t> GetQueueFamilyIndices();
    static uint32_t GetGraphicsQueueFamilyIndex();
    static uint32_t GetTransferQueueFamilyIndex();

    static vk::Queue GetPresentQueue();
    static vk::Queue GetGraphicsQueue();
    static vk::Queue GetMipQueue();
    static vk::Queue GetTransferQueue();

    static bool HasMipQueue();
    static bool HasTransferQueue();

    static VmaAllocator GetAllocator();

    static const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR &GetRayTracingPipelineProperties();
    static const vk::PhysicalDeviceAccelerationStructurePropertiesKHR &GetAccelerationStructureProperties();

private:
    static struct PhysicalDevice
    {
        vk::PhysicalDevice Handle = nullptr;

        vk::PhysicalDeviceProperties Properties;
        std::vector<vk::QueueFamilyProperties> QueueFamilyProperties;
        vk::PhysicalDeviceMemoryProperties MemoryProperties;
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR RayTracingPipelineProperties;
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR AccelerationStructureProperties;
    } s_PhysicalDevice;

    static struct LogicalDevice
    {
        vk::Device Handle = nullptr;

        uint32_t PresentQueueFamilyIndex = vk::QueueFamilyIgnored;
        uint32_t GraphicsQueueFamilyIndex = vk::QueueFamilyIgnored;
        uint32_t MipQueueFamilyIndex = vk::QueueFamilyIgnored;
        uint32_t TransferQueueFamilyIndex = vk::QueueFamilyIgnored;
        vk::Queue PresentQueue = nullptr;
        vk::Queue GraphicsQueue = nullptr;
        vk::Queue MipQueue = nullptr;
        vk::Queue TransferQueue = nullptr;
    } s_LogicalDevice;

    static VmaAllocator s_Allocator;

private:
    static bool CheckSuitable(
        vk::PhysicalDevice device, const std::vector<const char *> &requestedExtensions,
        const std::vector<const char *> &requestedLayers
    );

    static void FindQueueFamilies(vk::SurfaceKHR surface);
    static void GetQueueCreateInfos(
        std::vector<std::vector<float>> &priorities, std::vector<vk::DeviceQueueCreateInfo> &createInfos
    );
    static void GetQueues();
};

}
