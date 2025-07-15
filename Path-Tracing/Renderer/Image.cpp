#include <vulkan/vulkan_format_traits.hpp>

#include "Core/Core.h"

#include "Application.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "Image.h"
#include "Renderer.h"
#include "Utils.h"

namespace PathTracing
{

Image::Image(
    vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags,
    vk::MemoryPropertyFlags memoryFlags
)
    : m_Format(format), m_Extent(extent)
{
    VkImageCreateInfo createInfo = vk::ImageCreateInfo(
        vk::ImageCreateFlags(), vk::ImageType::e2D, format, vk::Extent3D(extent, 1), 1, 1,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usageFlags
    );

    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkImage image = nullptr;
    VkResult result = vmaCreateImage(
        DeviceContext::GetAllocator(), &createInfo, &allocinfo, &image, &m_Allocation, nullptr
    );
    assert(result == VkResult::VK_SUCCESS);
    m_Handle = image;

    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
    vk::ImageViewCreateInfo viewCreateInfo =
        vk::ImageViewCreateInfo(vk::ImageViewCreateFlags(), m_Handle, vk::ImageViewType::e2D, format)
            .setSubresourceRange(range);

    m_View = DeviceContext::GetLogical().createImageView(viewCreateInfo);
}

Image::~Image()
{
    if (m_IsMoved)
        return;

    DeviceContext::GetLogical().destroyImageView(m_View);
    vmaDestroyImage(DeviceContext::GetAllocator(), m_Handle, m_Allocation);
}

Image::Image(Image &&image) noexcept
{
    image.m_IsMoved = true;

    m_Handle = image.m_Handle;
    m_Allocation = image.m_Allocation;
    m_View = image.m_View;
    m_Format = image.m_Format;
    m_Extent = image.m_Extent;
}

Image &Image::operator=(Image &&image) noexcept
{
    if (m_Handle != nullptr)
        this->~Image();

    static_assert(!std::is_polymorphic_v<Image>);
    new (this) Image(std::move(image));

    return *this;
}

vk::Image Image::GetHandle() const
{
    return m_Handle;
}

vk::ImageView Image::GetView() const
{
    return m_View;
}

void Image::UploadStaging(const uint8_t *data) const
{
    BufferBuilder builder;
    builder.SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc)
        .SetMemoryFlags(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    Buffer buffer = builder.CreateBuffer(m_Extent.width * m_Extent.height * vk::blockSize(m_Format), "Image Staging Buffer");

    buffer.Upload(data);

    Renderer::s_MainCommandBuffer.Begin();

    Transition(
        Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal
    );

    Renderer::s_MainCommandBuffer.CommandBuffer.copyBufferToImage(
        buffer.GetHandle(), m_Handle, vk::ImageLayout::eTransferDstOptimal,
        { vk::BufferImageCopy(
            0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1),
            vk::Offset3D(0, 0, 0), vk::Extent3D(m_Extent, 1)
        ) }
    );

    Transition(
        Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eShaderReadOnlyOptimal
    );

    Renderer::s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());
}

void Image::SetDebugName(std::string_view name) const
{
    Utils::SetDebugName(m_Handle, vk::ObjectType::eImage, name);
    Utils::SetDebugName(m_View, vk::ObjectType::eImageView, std::format("ImageView: {}", name));
}

vk::AccessFlags Image::GetAccessFlags(vk::ImageLayout layout)
{
    switch (layout)
    {
    case vk::ImageLayout::eUndefined:
    case vk::ImageLayout::ePresentSrcKHR:
        return vk::AccessFlagBits::eNone;
    case vk::ImageLayout::eColorAttachmentOptimal:
        return vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
    case vk::ImageLayout::eTransferSrcOptimal:
        return vk::AccessFlagBits::eTransferRead;
    case vk::ImageLayout::eTransferDstOptimal:
        return vk::AccessFlagBits::eTransferWrite;
    case vk::ImageLayout::eGeneral:
        return vk::AccessFlagBits::eNone;
    default:
        throw error("Unsupported layout transition");
    }
}

vk::PipelineStageFlagBits Image::GetPipelineStageFlags(vk::ImageLayout layout)
{
    switch (layout)
    {
    case vk::ImageLayout::eUndefined:
        return vk::PipelineStageFlagBits::eTopOfPipe;
    case vk::ImageLayout::eTransferSrcOptimal:
        return vk::PipelineStageFlagBits::eTransfer;
    case vk::ImageLayout::eTransferDstOptimal:
        return vk::PipelineStageFlagBits::eTransfer;
    case vk::ImageLayout::eColorAttachmentOptimal:
        return vk::PipelineStageFlagBits::eColorAttachmentOutput;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return vk::PipelineStageFlagBits::eRayTracingShaderKHR;
    case vk::ImageLayout::ePresentSrcKHR:
        return vk::PipelineStageFlagBits::eBottomOfPipe;
    case vk::ImageLayout::eGeneral:
        return vk::PipelineStageFlagBits::eAllCommands;
    default:
        throw error("Unsupported layout transition");
    }
}

void Image::Transition(vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo) const
{
    Transition(buffer, m_Handle, layoutFrom, layoutTo);
}

void Image::Transition(
    vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo
)
{
    vk::ImageMemoryBarrier barrier(
        Image::GetAccessFlags(layoutFrom), Image::GetAccessFlags(layoutTo), layoutFrom, layoutTo,
        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image,
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1)
    );

    buffer.pipelineBarrier(
        Image::GetPipelineStageFlags(layoutFrom), Image::GetPipelineStageFlags(layoutTo),
        vk::DependencyFlags(), {}, {}, { barrier }
    );
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

Image ImageBuilder::CreateImage(vk::Extent2D extent, std::string_view name) const
{
    Image image = CreateImage(extent);
    image.SetDebugName(name);
    return image;
}

std::unique_ptr<Image> ImageBuilder::CreateImageUnique(vk::Extent2D extent) const
{
    return std::make_unique<Image>(m_Format, extent, m_UsageFlags, m_MemoryFlags);
}

std::unique_ptr<Image> ImageBuilder::CreateImageUnique(vk::Extent2D extent, std::string_view name) const
{
    auto image = CreateImageUnique(extent);
    image->SetDebugName(name);
    return image;
}

}
