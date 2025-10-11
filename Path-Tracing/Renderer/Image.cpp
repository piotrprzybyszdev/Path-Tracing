#include <vulkan/vulkan_format_traits.hpp>

#include "Core/Core.h"

#include "Application.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "Image.h"
#include "Utils.h"

namespace PathTracing
{

static uint32_t ComputeMipLevels(vk::Extent2D extent)
{
    return std::floor(std::log2(std::max(extent.width, extent.height))) + 1;
}

Image::Image(
    vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t layers,
    uint32_t mipLevels, bool isCube, const std::string &name
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

    VmaAllocationInfo info;
    vmaGetAllocationInfo(DeviceContext::GetAllocator(), m_Allocation, &info);
    assert(result == VkResult::VK_SUCCESS);

    VkMemoryPropertyFlags memoryProperties;
    vmaGetMemoryTypeProperties(DeviceContext::GetAllocator(), info.memoryType, &memoryProperties);

    if (!(memoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
    {
        logger::warn("Image `{}` was allocated in RAM instead of VRAM", name);
        m_IsDevice = false;
    }

    vk::ImageSubresourceRange range(vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, layers);
    vk::ImageViewType viewType = isCube ? vk::ImageViewType::eCube : vk::ImageViewType::e2D;
    vk::ImageViewCreateInfo viewCreateInfo =
        vk::ImageViewCreateInfo(vk::ImageViewCreateFlags(), m_Handle, viewType, format)
            .setSubresourceRange(range);

    m_View = DeviceContext::GetLogical().createImageView(viewCreateInfo);

    SetDebugName(name);
}

Image::Image(
    vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t layers, bool mips,
    bool isCube, const std::string &name
)
    : Image(format, extent, usageFlags, layers, mips ? ComputeMipLevels(extent) : 1, isCube, name)
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
    : m_Handle(image.m_Handle), m_Allocation(image.m_Allocation), m_View(image.m_View),
      m_Format(image.m_Format), m_Extent(image.m_Extent), m_MipLevels(image.m_MipLevels),
      m_Layers(image.m_Layers)
{
    image.m_IsMoved = true;
}

Image &Image::operator=(Image &&image) noexcept
{
    std::destroy_at(this);
    std::construct_at(this, std::move(image));
    return *this;
}

vk::Extent2D Image::GetExtent() const
{
    return m_Extent;
}

vk::Image Image::GetHandle() const
{
    return m_Handle;
}

vk::ImageView Image::GetView() const
{
    return m_View;
}

vk::Format Image::GetFormat() const
{
    return m_Format;
}

uint32_t Image::GetMip(vk::Extent2D extent) const
{
    return m_MipLevels - ComputeMipLevels(extent);
}

void Image::UploadFromBuffer(
    vk::CommandBuffer commandBuffer, const Buffer &buffer, vk::Extent2D extent, uint32_t mip, uint32_t layer,
    uint32_t layerCount
) const
{
    TransitionMip(
        commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mip, layer,
        layerCount
    );

    commandBuffer.copyBufferToImage(
        buffer.GetHandle(), m_Handle, vk::ImageLayout::eTransferDstOptimal,
        { vk::BufferImageCopy(
            0, 0, 0, GetMipLayer(mip, layer, layerCount), vk::Offset3D(0, 0, 0), vk::Extent3D(extent, 1)
        ) }
    );
}

/* unused mips ... | baseMip | ... | destMip | unused mips ... */
void Image::Scale(
    vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const Buffer &buffer, vk::Extent2D extent,
    uint32_t destMip
) const
{
    const uint32_t baseMip = GetMip(extent);

    UploadFromBuffer(transferBuffer, buffer, extent, baseMip);
    GenerateMips(mipBuffer, vk::ImageLayout::eTransferSrcOptimal, baseMip, destMip);
}

void Image::UploadStaging(
    vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const Buffer &buffer,
    const Image &temporary, vk::Extent2D extent, vk::ImageLayout layout
) const
{
    UploadStaging(mipBuffer, transferBuffer, buffer, temporary, extent, layout, 0, m_Layers);
}

void Image::UploadStaging(
    vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const Buffer &buffer,
    const Image &temporary, vk::Extent2D extent, vk::ImageLayout layout, uint32_t layer, uint32_t layerCount
) const
{
    assert(buffer.GetSize() >= GetByteSize(m_Extent, m_Format, layerCount));
    auto createBlitInfo = [this, &temporary](vk::ImageBlit2 &imageBlit) {
        return vk::BlitImageInfo2(
            temporary.GetHandle(), vk::ImageLayout::eTransferSrcOptimal, m_Handle,
            vk::ImageLayout::eTransferDstOptimal, imageBlit, vk::Filter::eLinear
        );
    };

    if (extent != m_Extent)
    {
        assert(layerCount == 1);
        assert(temporary.GetExtent() >= extent);

        const uint32_t destMip = temporary.GetMip(m_Extent) - 1;
        const vk::Extent2D destExtent(m_Extent.width * 2, m_Extent.height * 2);
        temporary.Scale(mipBuffer, transferBuffer, buffer, extent, destMip);

        TransitionMip(
            mipBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 0, layer, layerCount
        );

        vk::ImageBlit2 imageBlit(
            temporary.GetMipLayer(destMip), temporary.GetMipLevelArea(destExtent), GetMipLayer(0, layer),
            GetMipLevelArea()
        );

        mipBuffer.blitImage2(createBlitInfo(imageBlit));
    }
    else if (temporary.m_Format != m_Format)
    {
        assert(temporary.m_Layers >= layer + layerCount);

        temporary.UploadFromBuffer(transferBuffer, buffer, extent, 0, layer, layerCount);
        temporary.TransitionMip(
            mipBuffer, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, 0, layer,
            layerCount
        );
        TransitionMip(
            mipBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 0, layer, layerCount
        );

        vk::ImageBlit2 imageBlit(
            temporary.GetMipLayer(0, layer, layerCount), temporary.GetMipLevelArea(m_Extent),
            GetMipLayer(0, layer, layerCount), GetMipLevelArea()
        );

        mipBuffer.blitImage2(createBlitInfo(imageBlit));
    }
    else
        UploadFromBuffer(transferBuffer, buffer, extent, 0, layer, layerCount);

    GenerateFullMips(mipBuffer, layout, layer, layerCount);
}

void Image::GenerateFullMips(
    vk::CommandBuffer commandBuffer, vk::ImageLayout layout, uint32_t layer, uint32_t layerCount
) const
{
    GenerateMips(commandBuffer, layout, 0, m_MipLevels - 1, layer, layerCount);
}

void Image::GenerateMips(
    vk::CommandBuffer commandBuffer, vk::ImageLayout layout, uint32_t fromMip, uint32_t toMip, uint32_t layer,
    uint32_t layerCount
) const
{
    if (m_MipLevels == 1)
    {
        Transition(commandBuffer, vk::ImageLayout::eTransferDstOptimal, layout, layer, layerCount);
        return;
    }

    TransitionMip(
        commandBuffer, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, fromMip,
        layer, layerCount
    );

    for (uint32_t level = fromMip + 1; level <= toMip; level++)
    {
        vk::ImageBlit imageBlit(
            GetMipLayer(level - 1, layer, layerCount), GetMipLevelArea(level - 1),
            GetMipLayer(level, layer, layerCount), GetMipLevelArea(level)
        );

        TransitionMip(
            commandBuffer, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, level, layer,
            layerCount
        );

        commandBuffer.blitImage(
            m_Handle, vk::ImageLayout::eTransferSrcOptimal, m_Handle, vk::ImageLayout::eTransferDstOptimal,
            imageBlit, vk::Filter::eLinear
        );

        TransitionMip(
            commandBuffer, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, level,
            layer, layerCount
        );
    }

    if (fromMip > 0)
        Transition(commandBuffer, m_Handle, vk::ImageLayout::eUndefined, layout, 0, fromMip, layer);

    Transition(
        commandBuffer, m_Handle, vk::ImageLayout::eTransferSrcOptimal, layout, fromMip, toMip - fromMip + 1,
        layer, layerCount
    );

    if (m_MipLevels - toMip - 1 > 0)
        Transition(
            commandBuffer, m_Handle, vk::ImageLayout::eUndefined, layout, toMip + 1, m_MipLevels - toMip - 1,
            layer, layerCount
        );
}

void Image::SetDebugName(const std::string &name) const
{
    Utils::SetDebugName(m_Handle, name);
    Utils::SetDebugName(m_View, std::format("ImageView: {}", name));
}

vk::AccessFlags2 Image::GetAccessFlags(vk::ImageLayout layout)
{
    switch (layout)
    {
    case vk::ImageLayout::eUndefined:
        return vk::AccessFlagBits2::eNone;
    case vk::ImageLayout::eAttachmentOptimal:
        return vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return vk::AccessFlagBits2::eShaderSampledRead;
    case vk::ImageLayout::eTransferSrcOptimal:
        return vk::AccessFlagBits2::eTransferRead;
    case vk::ImageLayout::eTransferDstOptimal:
        return vk::AccessFlagBits2::eTransferWrite;
    case vk::ImageLayout::ePresentSrcKHR:
        return vk::AccessFlagBits2::eNone;
    case vk::ImageLayout::eGeneral:
        return vk::AccessFlagBits2::eNone;
    default:
        throw error("Unsupported layout transition");
    }
}

vk::PipelineStageFlags2 Image::GetPipelineStageFlags(vk::ImageLayout layout)
{
    switch (layout)
    {
    case vk::ImageLayout::eUndefined:
        return vk::PipelineStageFlagBits2::eNone;
    case vk::ImageLayout::eTransferSrcOptimal:
        return vk::PipelineStageFlagBits2::eTransfer;
    case vk::ImageLayout::eTransferDstOptimal:
        return vk::PipelineStageFlagBits2::eTransfer;
    case vk::ImageLayout::eAttachmentOptimal:
        return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
    case vk::ImageLayout::eShaderReadOnlyOptimal:
        return vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
    case vk::ImageLayout::ePresentSrcKHR:
        return vk::PipelineStageFlagBits2::eAllCommands;
    case vk::ImageLayout::eGeneral:
        return vk::PipelineStageFlagBits2::eAllCommands;
    default:
        throw error("Unsupported layout transition");
    }
}

void Image::Transition(
    vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t layer,
    uint32_t layerCount
) const
{
    Transition(buffer, m_Handle, layoutFrom, layoutTo, 0, m_MipLevels, layer, layerCount);
}

void Image::TransitionMip(
    vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t mipLevel,
    uint32_t layer, uint32_t layerCount
) const
{
    Transition(buffer, m_Handle, layoutFrom, layoutTo, mipLevel, 1, layer, layerCount);
}

void Image::Transition(
    vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
    uint32_t baseMipLevel, uint32_t mipLevels, uint32_t layer, uint32_t layerCount
)
{
    vk::ImageMemoryBarrier2 barrier(
        Image::GetPipelineStageFlags(layoutFrom), Image::GetAccessFlags(layoutFrom),
        Image::GetPipelineStageFlags(layoutTo), Image::GetAccessFlags(layoutTo), layoutFrom, layoutTo,
        vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, image,
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, baseMipLevel, mipLevels, layer, layerCount)
    );

    vk::DependencyInfo dependency;
    dependency.setImageMemoryBarriers(barrier);

    buffer.pipelineBarrier2(dependency);
}

std::array<vk::Offset3D, 2> Image::GetMipLevelArea(vk::Extent2D extent, uint32_t level)
{
    return { vk::Offset3D(), vk::Offset3D(extent.width >> level, extent.height >> level, 1) };
}

std::array<vk::Offset3D, 2> Image::GetMipLevelArea(uint32_t level) const
{
    return GetMipLevelArea(vk::Extent2D(m_Extent.width >> level, m_Extent.height >> level));
}

vk::ImageSubresourceLayers Image::GetMipLayer(uint32_t level, uint32_t layer, uint32_t layerCount) const
{
    return { vk::ImageAspectFlagBits::eColor, level, layer, layerCount };
}

vk::DeviceSize Image::GetByteSize(vk::Extent2D extent, vk::Format format, uint32_t layers)
{
    return static_cast<vk::DeviceSize>(layers) * extent.width * extent.height * vk::blockSize(format);
}

bool Image::IsDevice() const
{
    return m_IsDevice;
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

ImageBuilder &ImageBuilder::EnableMips(bool value)
{
    m_Mips = value;
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

Image ImageBuilder::CreateImage(vk::Extent2D extent, const std::string &name) const
{
    return Image(m_Format, extent, m_UsageFlags, m_Layers, m_Mips, m_Cube, name);
}

std::unique_ptr<Image> ImageBuilder::CreateImageUnique(vk::Extent2D extent, const std::string &name) const
{
    return std::make_unique<Image>(m_Format, extent, m_UsageFlags, m_Layers, m_Mips, m_Cube, name);
}

}
