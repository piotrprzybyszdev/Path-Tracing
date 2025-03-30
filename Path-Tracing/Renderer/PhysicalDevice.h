#pragma once

#include <vulkan/vulkan.hpp>

#include "Buffer.h"
#include "Image.h"
#include "ShaderLibrary.h"

namespace PathTracing
{

class PhysicalDevice
{
public:
    PhysicalDevice(vk::PhysicalDevice handle = nullptr, vk::SurfaceKHR surface = nullptr);
    ~PhysicalDevice();

    uint32_t GetQueueFamilyIndex(vk::QueueFlags flags) const;
    uint32_t FindMemoryTypeIndex(vk::MemoryRequirements requirements, vk::MemoryPropertyFlags flags) const;
    uint32_t GetAlignedShaderGroupHandleSize() const;

private:
    vk::PhysicalDevice m_Handle;
    vk::SurfaceKHR m_Surface;

    vk::PhysicalDeviceProperties m_Properties;
    vk::PhysicalDeviceMemoryProperties m_MemoryProperties;
    std::vector<vk::QueueFamilyProperties> m_QueueFamilyProperties;
    vk::PhysicalDeviceRayTracingPipelinePropertiesKHR m_RayTracingPipelineProperties;

    friend class LogicalDevice;
};

}
