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

size_t Image::GetTextureMemoryRequirement(vk::Extent2D extent, vk::Format format)
{
    const vk::ImageUsageFlags usageFlags = vk::ImageUsageFlagBits::eTransferSrc |
                                           vk::ImageUsageFlagBits::eTransferDst |
                                           vk::ImageUsageFlagBits::eSampled;

    VkImageCreateInfo createInfo = vk::ImageCreateInfo(
        vk::ImageCreateFlags(), vk::ImageType::e2D, format, vk::Extent3D(extent, 1), ComputeMipLevels(extent),
        1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usageFlags
    );

    VmaAllocationCreateInfo allocinfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    vk::Image image = DeviceContext::GetLogical().createImage(createInfo);
    const size_t size = DeviceContext::GetLogical().getImageMemoryRequirements(image).size;
    DeviceContext::GetLogical().destroyImage(image);

    return size;
}

size_t Image::GetImageMemoryBudget()
{
    const vk::ImageUsageFlags usageFlags = vk::ImageUsageFlagBits::eTransferSrc |
                                           vk::ImageUsageFlagBits::eTransferDst |
                                           vk::ImageUsageFlagBits::eSampled;

    vk::Extent2D extent(1024u, 1024u);
    VkImageCreateInfo createInfo = vk::ImageCreateInfo(
        vk::ImageCreateFlags(), vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D(extent, 1),
        ComputeMipLevels(extent), 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usageFlags
    );

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    uint32_t memoryTypeIndex;
    VkResult result = vmaFindMemoryTypeIndexForImageInfo(DeviceContext::GetAllocator(), &createInfo, &allocInfo, &memoryTypeIndex);
    assert(result == VK_SUCCESS);

    const auto properties = DeviceContext::GetPhysical().getMemoryProperties();
    const uint32_t heapIndex = properties.memoryTypes[memoryTypeIndex].heapIndex;
    return properties.memoryHeaps[heapIndex].size;
}

Image::Image(
    vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t layers,
    uint32_t mipLevels, bool isCube, const std::string &name
)
    : m_Format(format), m_Extent(extent), m_Layers(layers), m_MipLevels(mipLevels)
{
    assert(extent.width > 0 && extent.height > 0);
    if (isCube)
        assert(layers == 6);

    vk::ImageCreateFlags flags = isCube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags();
    VkImageCreateInfo createInfo = vk::ImageCreateInfo(
        flags, vk::ImageType::e2D, format, vk::Extent3D(extent, 1), mipLevels, layers,
        vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, usageFlags
    );

    VmaAllocationCreateInfo allocInfo = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    VkImage image = nullptr;
    VkResult result = vmaCreateImage(
        DeviceContext::GetAllocator(), &createInfo, &allocInfo, &image, &m_Allocation, nullptr
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

uint32_t Image::GetMipLevels() const
{
    return m_MipLevels;
}

uint32_t Image::GetMip(vk::Extent2D extent) const
{
    return m_MipLevels - std::ceil(std::log2(std::max(extent.width, extent.height))) - 1;
}

vk::Extent2D Image::GetMipExtent(vk::Extent2D extent, uint32_t mip)
{
    return vk::Extent2D(extent.width / std::pow(2, mip), extent.height / std::pow(2, mip));
}

vk::DeviceSize Image::GetSize(vk::Extent2D extent, vk::Format format)
{
    size_t texels = static_cast<size_t>(extent.width) * extent.height;
    size_t texelsPerBlock =
        vk::blockExtent(format)[0] * vk::blockExtent(format)[1] * vk::blockExtent(format)[2];
    return std::ceil(static_cast<float>(texels) / texelsPerBlock) * vk::blockSize(format);
}

vk::Extent2D Image::GetMipExtent(uint32_t mip) const
{
    assert(mip < m_MipLevels);
    return GetMipExtent(m_Extent, mip);
}

size_t Image::GetMipSize(uint32_t mip) const
{
    return GetSize(GetMipExtent(mip), m_Format);
}

void Image::CopyMipTo(vk::CommandBuffer commandBuffer, const Image &image, uint32_t mip) const
{
    assert(m_Layers == 1);
    vk::Offset3D offset(0, 0, 0);

    assert(GetMipExtent(mip) >= image.GetExtent());

    vk::ImageCopy region(
        GetMipLayer(mip), offset, image.GetMipLayer(0), offset, vk::Extent3D(image.GetExtent(), 1)
    );

    Transition(
        commandBuffer, image.GetHandle(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
        0, 1
    );

    commandBuffer.copyImage(
        m_Handle, vk::ImageLayout::eTransferSrcOptimal, image.GetHandle(),
        vk::ImageLayout::eTransferDstOptimal, region
    );
}

void Image::UploadFromBuffer(
    vk::CommandBuffer commandBuffer, const Buffer &buffer, vk::DeviceSize offset, vk::Extent2D extent,
    uint32_t baseMip, uint32_t mips
) const
{
    Transition(
        commandBuffer, m_Handle, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, baseMip,
        mips
    );

    vk::Extent2D mipExtent = extent;
    for (uint32_t mip = baseMip; mip < baseMip + mips; mip++)
    {
        commandBuffer.copyBufferToImage(
            buffer.GetHandle(), m_Handle, vk::ImageLayout::eTransferDstOptimal,
            { vk::BufferImageCopy(
                offset, 0, 0, GetMipLayer(mip), vk::Offset3D(0, 0, 0),
                vk::Extent3D(mipExtent, 1)
            ) }
        );

        offset += Image::GetSize(mipExtent, m_Format);
        mipExtent.width /= 2;
        mipExtent.height /= 2;
    }

    Transition(
        commandBuffer, m_Handle, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferDstOptimal,
        baseMip, mips
    );
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

void Image::TransitionWithQueueChange(
    vk::CommandBuffer bufferFrom, vk::CommandBuffer bufferTo, vk::ImageLayout layoutFrom,
    vk::ImageLayout layoutTo, vk::PipelineStageFlags2 stageFrom, vk::PipelineStageFlags2 stageTo,
    vk::AccessFlags2 accessFrom, vk::AccessFlags2 accessTo, uint32_t queueFamilyIndexFrom,
    uint32_t queueFamilyIndexTo
) const
{
    if (bufferFrom == bufferTo)
    {
        Transition(bufferFrom, m_Handle, layoutFrom, layoutTo, stageFrom, stageTo, accessFrom, accessTo);
        return;
    }

    vk::ImageMemoryBarrier2 barrier(
        stageFrom, accessFrom, stageTo, accessTo, layoutFrom, layoutTo, queueFamilyIndexFrom,
        queueFamilyIndexTo, m_Handle,
        vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, m_MipLevels, 0, m_Layers)
    );

    vk::DependencyInfo dependency;
    dependency.setImageMemoryBarriers(barrier);

    if (bufferFrom != nullptr)
        bufferFrom.pipelineBarrier2(dependency);
    if (bufferTo != nullptr)
        bufferTo.pipelineBarrier2(dependency);
}

void Image::Transition(
    vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
    uint32_t baseMipLevel, uint32_t mipLevels, uint32_t layer, uint32_t layerCount
)
{
    Transition(
        buffer, image, layoutFrom, layoutTo, Image::GetPipelineStageFlags(layoutFrom),
        Image::GetPipelineStageFlags(layoutTo), Image::GetAccessFlags(layoutFrom),
        Image::GetAccessFlags(layoutTo), baseMipLevel, mipLevels, layer, layerCount
    );
}

void Image::Transition(
    vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
    vk::PipelineStageFlags2 stageFrom, vk::PipelineStageFlags2 stageTo, vk::AccessFlags2 accessFrom,
    vk::AccessFlags2 accessTo, uint32_t baseMipLevel, uint32_t mipLevels, uint32_t layer, uint32_t layerCount
)
{
    vk::ImageMemoryBarrier2 barrier(
        stageFrom, accessFrom, stageTo, accessTo, layoutFrom, layoutTo, vk::QueueFamilyIgnored,
        vk::QueueFamilyIgnored, image,
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
