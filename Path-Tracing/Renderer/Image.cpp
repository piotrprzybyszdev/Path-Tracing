#include <Core/Core.h>

#include "Image.h"
#include "PhysicalDevice.h"

namespace PathTracing
{

Image::Image(
    vk::Device device, const PhysicalDevice &physicalDevice, vk::Format format, uint32_t width, uint32_t height,
    vk::ImageUsageFlags usageFlags, vk::MemoryPropertyFlags memoryFlags
)
    : m_Device(device), m_Width(width), m_Height(height), m_Format(format)
{
    vk::ImageCreateInfo createInfo(
        vk::ImageCreateFlags(), vk::ImageType::e2D, format, vk::Extent3D(width, height, 1), 1, 1,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usageFlags
    );

    m_Handle = m_Device.createImage(createInfo);

    vk::MemoryRequirements requirements = m_Device.getImageMemoryRequirements(m_Handle);

    uint32_t memoryTypeIndex = physicalDevice.FindMemoryTypeIndex(requirements, memoryFlags);

    vk::MemoryAllocateInfo memoryAllocateInfo(requirements.size, memoryTypeIndex);
    m_Memory = m_Device.allocateMemory(memoryAllocateInfo);
    m_Device.bindImageMemory(m_Handle, m_Memory, 0);

    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::ImageViewCreateInfo viewCreateInfo =
        vk::ImageViewCreateInfo(
            vk::ImageViewCreateFlags(), m_Handle, vk::ImageViewType::e2D, vk::Format::eB8G8R8A8Unorm
        )
            .setSubresourceRange(range);

    m_View = m_Device.createImageView(viewCreateInfo);
}

Image::~Image()
{
    m_Device.destroyImageView(m_View);
    m_Device.destroyImage(m_Handle);
    m_Device.freeMemory(m_Memory);
}

vk::Image Image::GetHandle() const
{
    return m_Handle;
}

vk::ImageView Image::GetView() const
{
    return m_View;
}

ImageBuilder::ImageBuilder(vk::Device device, const PhysicalDevice &physicalDevice)
    : m_Device(device), m_PhysicalDevice(physicalDevice)
{
}

ImageBuilder &ImageBuilder::SetFormat(vk::Format format)
{
    m_Format = format;
    return *this;
}

ImageBuilder &ImageBuilder::SetUsageFlags(vk::ImageUsageFlags usageFlags)
{
    m_UsageFlags = usageFlags;
    return *this;
}

ImageBuilder &ImageBuilder::SetMemoryFlags(vk::MemoryPropertyFlags memoryFlags)
{
    m_MemoryFlags = memoryFlags;
    return *this;
}

ImageBuilder &ImageBuilder::ResetFlags()
{
    m_Format = vk::Format();
    m_UsageFlags = vk::ImageUsageFlags();
    m_MemoryFlags = vk::MemoryPropertyFlags();
    return *this;
}

Image ImageBuilder::CreateImage(uint32_t width, uint32_t height) const
{
    return Image(m_Device, m_PhysicalDevice, m_Format, width, height, m_UsageFlags, m_MemoryFlags);
}

std::unique_ptr<Image> ImageBuilder::CreateImageUnique(uint32_t width, uint32_t height) const
{
    return std::make_unique<Image>(
        m_Device, m_PhysicalDevice, m_Format, width, height, m_UsageFlags, m_MemoryFlags
    );
}

}