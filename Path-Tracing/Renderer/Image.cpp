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

static uint32_t ComputeMipLevels(vk::Extent2D extent)
{
    return std::floor(std::log2(std::max(extent.width, extent.height))) + 1;
}

Image::Image(
    vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t layers,
    uint32_t mipLevels, bool isCube
)
    : m_Format(format), m_Extent(extent), m_Layers(layers), m_MipLevels(mipLevels)
{
    if (isCube)
        assert(layers == 6);

    vk::ImageCreateFlags flags = isCube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags();
    VkImageCreateInfo createInfo = vk::ImageCreateInfo(
        flags, vk::ImageType::e2D, format, vk::Extent3D(extent, 1), mipLevels, layers,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usageFlags
    );

    VmaAllocationCreateInfo allocinfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkImage image = nullptr;
    VkResult result = vmaCreateImage(
        DeviceContext::GetAllocator(), &createInfo, &allocinfo, &image, &m_Allocation, nullptr
    );
    assert(result == VkResult::VK_SUCCESS);
    m_Handle = image;

    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, layers);
    vk::ImageViewType viewType = isCube ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
    vk::ImageViewCreateInfo viewCreateInfo =
        vk::ImageViewCreateInfo(vk::ImageViewCreateFlags(), m_Handle, viewType, format)
            .setSubresourceRange(range);

    m_View = DeviceContext::GetLogical().createImageView(viewCreateInfo);
}

Image::Image(
    vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t layers, bool mips, bool isCube
)
    : Image(format, extent, usageFlags, layers, mips ? ComputeMipLevels(extent) : 1, isCube)
{
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
    m_MipLevels = image.m_MipLevels;
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

void Image::UploadStaging(const std::byte *data, vk::Extent2D extent, vk::ImageLayout layout, uint32_t layer)
    const
{
    assert(extent.width >= m_Extent.width && extent.height >= m_Extent.height);

    const uint32_t baseMipLevel = ComputeMipLevels(extent);
    const uint32_t destMipLevel = ComputeMipLevels(m_Extent);

    if (baseMipLevel != destMipLevel)
    {
        // We have to generate temporary mips to scale the image from `baseMipLevel` to `destMipLevel`
        const uint32_t temporaryMipLevels = baseMipLevel - destMipLevel;

        Image temporary(
            m_Format, extent,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
                vk::ImageUsageFlagBits::eTransferDst,
            1, temporaryMipLevels, false
        );
        Utils::SetDebugName(temporary.GetHandle(), "Image Scaling Helper Image");
        temporary.UploadStaging(data, extent, vk::ImageLayout::eTransferSrcOptimal);

        // Copy from `image's` lowest mip to `this` image
        vk::ImageBlit imageBlit(
            temporary.GetMipLayer(temporaryMipLevels - 1),
            temporary.GetMipLevelArea(temporaryMipLevels - 1), GetMipLayer(0, layer), GetMipLevelArea(0)
        );

        Renderer::s_MainCommandBuffer.Begin();

        TransitionMip(
            Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, 0, layer
        );

        Renderer::s_MainCommandBuffer.CommandBuffer.blitImage(
            temporary.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, m_Handle,
            vk::ImageLayout::eTransferDstOptimal, imageBlit, vk::Filter::eLinear
        );

        GenerateMips(layer, layout);

        Renderer::s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());

        return;
    }

    auto content = std::span(data, extent.width * extent.height * vk::blockSize(m_Format));
    Buffer buffer = BufferBuilder()
                        .SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc)
                        .CreateHostBuffer(content, "Image Staging Buffer");

    Renderer::s_MainCommandBuffer.Begin();

    TransitionMip(
        Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eUndefined,
        vk::ImageLayout::eTransferDstOptimal, 0, layer
    );

    Renderer::s_MainCommandBuffer.CommandBuffer.copyBufferToImage(
        buffer.GetHandle(), m_Handle, vk::ImageLayout::eTransferDstOptimal,
        { vk::BufferImageCopy(
            0, 0, 0, vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, layer, 1),
            vk::Offset3D(0, 0, 0), vk::Extent3D(m_Extent, 1)
        ) }
    );

    GenerateMips(layer, layout);

    Renderer::s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());
}

void Image::GenerateMips(uint32_t layer, vk::ImageLayout layout) const
{
    if (m_MipLevels == 1)
    {
        Transition(
            Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eTransferDstOptimal, layout, layer
        );
        return;
    }

#ifndef NDEBUG
    auto properties = DeviceContext::GetPhysical().getFormatProperties(m_Format).bufferFeatures;
    if (properties & vk::FormatFeatureFlagBits::eBlitSrc & vk::FormatFeatureFlagBits::eBlitDst)
        throw error(std::format("Can't geenrate mip maps for texture format {}", vk::to_string(m_Format)));
#endif

    TransitionMip(
        Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eTransferDstOptimal,
        vk::ImageLayout::eTransferSrcOptimal, 0, layer
    );

    for (uint32_t level = 1; level < m_MipLevels; level++)
    {
        vk::ImageBlit imageBlit(
            GetMipLayer(level - 1, layer), GetMipLevelArea(level - 1), GetMipLayer(level, layer), GetMipLevelArea(level)
        );

        TransitionMip(
            Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, level, layer
        );

        Renderer::s_MainCommandBuffer.CommandBuffer.blitImage(
            m_Handle, vk::ImageLayout::eTransferSrcOptimal, m_Handle, vk::ImageLayout::eTransferDstOptimal,
            imageBlit, vk::Filter::eLinear
        );

        TransitionMip(
            Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eTransferSrcOptimal, level, layer
        );
    }

    Transition(
        Renderer::s_MainCommandBuffer.CommandBuffer, vk::ImageLayout::eTransferSrcOptimal, layout, layer
    );
}

void Image::SetDebugName(const std::string &name) const
{
    Utils::SetDebugName(m_Handle, name);
    Utils::SetDebugName(m_View, std::format("ImageView: {}", name));
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

void Image::Transition(
    vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t layer
) const
{
    Transition(buffer, m_Handle, layoutFrom, layoutTo, 0, m_MipLevels, layer);
}

void Image::TransitionMip(
    vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t mipLevel,
    uint32_t layer
) const
{
    Transition(buffer, m_Handle, layoutFrom, layoutTo, mipLevel, 1, layer);
}

void Image::Transition(
    vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
    uint32_t baseMipLevel, uint32_t mipLevels, uint32_t layer
)
{
    vk::ImageMemoryBarrier barrier(
        Image::GetAccessFlags(layoutFrom), Image::GetAccessFlags(layoutTo), layoutFrom, layoutTo,
        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image,
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, baseMipLevel, mipLevels, layer, 1)
    );

    buffer.pipelineBarrier(
        Image::GetPipelineStageFlags(layoutFrom), Image::GetPipelineStageFlags(layoutTo),
        vk::DependencyFlags(), {}, {}, { barrier }
    );
}

std::array<vk::Offset3D, 2> Image::GetMipLevelArea(uint32_t level) const
{
    return { { {},
               { static_cast<int32_t>(m_Extent.width >> level),
                 static_cast<int32_t>(m_Extent.height >> level), 1 } } };
}

vk::ImageSubresourceLayers Image::GetMipLayer(uint32_t level, uint32_t layer) const
{
    return { vk::ImageAspectFlagBits::eColor, level, layer, 1 };
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

ImageBuilder &ImageBuilder::EnableMips()
{
    m_Mips = true;
    return *this;
}

ImageBuilder &ImageBuilder::SetLayers(uint32_t layers)
{
    m_Layers = layers;
    if (m_Layers != 6)
        m_Cube = false;
    return *this;
}

ImageBuilder &ImageBuilder::EnableCube()
{
    m_Cube = true;
    m_Layers = 6;
    return *this;
}

ImageBuilder &ImageBuilder::ResetFlags()
{
    m_Format = vk::Format();
    m_UsageFlags = vk::ImageUsageFlags();
    return *this;
}

Image ImageBuilder::CreateImage(vk::Extent2D extent) const
{
    return Image(m_Format, extent, m_UsageFlags, m_Layers, m_Mips, m_Cube);
}

Image ImageBuilder::CreateImage(vk::Extent2D extent, const std::string &name) const
{
    Image image = CreateImage(extent);
    image.SetDebugName(name);
    return image;
}

std::unique_ptr<Image> ImageBuilder::CreateImageUnique(vk::Extent2D extent) const
{
    return std::make_unique<Image>(m_Format, extent, m_UsageFlags, m_Layers, m_Mips, m_Cube);
}

std::unique_ptr<Image> ImageBuilder::CreateImageUnique(vk::Extent2D extent, const std::string &name) const
{
    auto image = CreateImageUnique(extent);
    image->SetDebugName(name);
    return image;
}

}
