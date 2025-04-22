#include "Core/Core.h"

#include "PhysicalDevice.h"

namespace PathTracing
{

PhysicalDevice::PhysicalDevice(vk::PhysicalDevice handle, vk::SurfaceKHR surface)
    : m_Handle(handle), m_Surface(surface)
{
    if (m_Handle == nullptr)
        return;

    m_Properties = m_Handle.getProperties();
    m_MemoryProperties = m_Handle.getMemoryProperties();
    m_QueueFamilyProperties = m_Handle.getQueueFamilyProperties();
    m_RayTracingPipelineProperties =
        m_Handle
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>(
            )
            .get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

    logger::info("Selected physical device: {}", m_Properties.deviceName.data());

    for (uint32_t index = 0; index < m_QueueFamilyProperties.size(); index++)
        logger::info(
            "Found queue family at index {} with properties: {}", index,
            vk::to_string(m_QueueFamilyProperties[index].queueFlags)
        );
}

PhysicalDevice::~PhysicalDevice()
{
}

uint32_t PhysicalDevice::FindMemoryTypeIndex(
    vk::MemoryRequirements requirements, vk::MemoryPropertyFlags flags
) const
{
    std::optional<int> memoryTypeIndex;
    for (int index = 0; index < m_MemoryProperties.memoryTypeCount; index++)
    {
        const vk::MemoryType memoryType = m_MemoryProperties.memoryTypes[index];

        if (((0x1u << index) & requirements.memoryTypeBits) == 0x0u)
            continue;
        if ((memoryType.propertyFlags & flags) == flags)
        {
            memoryTypeIndex = index;
            break;
        }
    }

    if (!memoryTypeIndex.has_value())
        throw PathTracing::error("No suitable memory type found");

    return memoryTypeIndex.value();
}

uint32_t PhysicalDevice::GetAlignedShaderGroupHandleSize() const
{
    auto alignedSize = [](uint32_t size, uint32_t alignment) {
        return (size + alignment - 1) & ~(alignment - 1);
    };

    return alignedSize(
        m_RayTracingPipelineProperties.shaderGroupHandleSize,
        m_RayTracingPipelineProperties.shaderGroupHandleAlignment
    );
}

uint32_t PhysicalDevice::GetQueueFamilyIndex(vk::QueueFlags flags) const
{
    for (uint32_t index = 0; index < m_QueueFamilyProperties.size(); index++)
    {
        if (m_Handle.getSurfaceSupportKHR(index, m_Surface) == vk::True &&
            (m_QueueFamilyProperties[index].queueFlags & flags) == flags)
            return index;
    }

    throw error("No appropriate queue family found");
}

}
