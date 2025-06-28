#include "Core/Core.h"

#include "DeviceContext.h"
#include "Image.h"

namespace PathTracing
{

Image::Image(
    vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags,
    vk::MemoryPropertyFlags memoryFlags
)
{
    vk::ImageCreateInfo createInfo(
        vk::ImageCreateFlags(), vk::ImageType::e2D, format, vk::Extent3D(extent, 1), 1, 1,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usageFlags
    );

    m_Handle = DeviceContext::GetLogical().createImage(createInfo);

    vk::MemoryRequirements requirements = DeviceContext::GetLogical().getImageMemoryRequirements(m_Handle);

    uint32_t memoryTypeIndex = DeviceContext::FindMemoryTypeIndex(requirements, memoryFlags);

    vk::MemoryAllocateInfo memoryAllocateInfo(requirements.size, memoryTypeIndex);
    m_Memory = DeviceContext::GetLogical().allocateMemory(memoryAllocateInfo);
    DeviceContext::GetLogical().bindImageMemory(m_Handle, m_Memory, 0);

    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::ImageViewCreateInfo viewCreateInfo =
        vk::ImageViewCreateInfo(vk::ImageViewCreateFlags(), m_Handle, vk::ImageViewType::e2D, format)
            .setSubresourceRange(range);

    m_View = DeviceContext::GetLogical().createImageView(viewCreateInfo);
}

Image::~Image()
{
    DeviceContext::GetLogical().destroyImageView(m_View);
    DeviceContext::GetLogical().destroyImage(m_Handle);
    DeviceContext::GetLogical().freeMemory(m_Memory);
}

vk::Image Image::GetHandle() const
{
    return m_Handle;
}

vk::ImageView Image::GetView() const
{
    return m_View;
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

Image ImageBuilder::CreateImage(vk::Extent2D extent) const
{
    return Image(m_Format, extent, m_UsageFlags, m_MemoryFlags);
}

std::unique_ptr<Image> ImageBuilder::CreateImageUnique(vk::Extent2D extent) const
{
    return std::make_unique<Image>(m_Format, extent, m_UsageFlags, m_MemoryFlags);
}

}
