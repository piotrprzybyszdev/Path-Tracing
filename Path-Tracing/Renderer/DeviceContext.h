#pragma once

#include <mutex>
#include <vector>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace PathTracing
{

class DeviceContext;

// GetLock() should be called everytime vulkan spec states
// that a command requires external synchronization
class Queue
{
public:
    uint32_t FamilyIndex = vk::QueueFamilyIgnored;
    vk::Queue Handle;

    [[nodiscard]] std::unique_lock<std::mutex> GetLock();
    void WaitIdle();

private:
    std::mutex m_Mutex;
    bool m_ShouldLock = false;

    friend DeviceContext;
};

class DeviceContext
{
public:
    static void Init(vk::Instance instance, vk::SurfaceKHR surface);
    static void Shutdown();

    static vk::PhysicalDevice GetPhysical();
    static vk::Device GetLogical();

    static std::vector<uint32_t> GetQueueFamilyIndices();

    // Present and graphics queues should be used by main thread only
    // Transfer and mip queues should be used by texture loading `submit thread` only
    static Queue &GetPresentQueue();
    static Queue &GetGraphicsQueue();
    static Queue &GetMipQueue();
    static Queue &GetTransferQueue();

    static bool HasMipQueue();
    static bool HasTransferQueue();

    static VmaAllocator GetAllocator();

    static const vk::PhysicalDeviceRayTracingPipelinePropertiesKHR &GetRayTracingPipelineProperties();
    static const vk::PhysicalDeviceAccelerationStructurePropertiesKHR &GetAccelerationStructureProperties();

private:
    static struct PhysicalDevice
    {
        vk::PhysicalDevice Handle = nullptr;

        vk::PhysicalDeviceProperties2 Properties;
        std::vector<vk::QueueFamilyProperties2> QueueFamilyProperties;
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR RayTracingPipelineProperties;
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR AccelerationStructureProperties;
    } s_PhysicalDevice;

    static struct LogicalDevice
    {
        vk::Device Handle = nullptr;

        Queue PresentQueue;
        Queue GraphicsQueue;
        Queue MipQueue;
        Queue TransferQueue;
    } s_LogicalDevice;

    static VmaAllocator s_Allocator;

private:
    static bool CheckSuitable(
        vk::PhysicalDevice device, const std::vector<const char *> &requestedExtensions
    );

    static void FindQueueFamilies(vk::SurfaceKHR surface);
    static void GetQueueCreateInfos(
        std::vector<std::vector<float>> &priorities, std::vector<vk::DeviceQueueCreateInfo> &createInfos
    );
    static void GetQueues();
};

}
