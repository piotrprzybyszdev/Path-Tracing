#pragma once

#include <span>
#include <string>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

namespace PathTracing
{

class Image
{
public:
    Image() = default;
    Image(vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t mipLevels);
    Image(vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, bool mips);
    
    ~Image();

    Image(Image &&image) noexcept;
    Image(const Image &image) = delete;

    Image &operator=(Image &&image) noexcept;
    Image &operator=(const Image &image) = delete;

    [[nodiscard]] vk::Image GetHandle() const;
    [[nodiscard]] vk::ImageView GetView() const;

    void UploadStaging(const std::byte *data, vk::Extent2D extent, vk::ImageLayout layout) const;

    void SetDebugName(const std::string &name) const;

    void Transition(vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo) const;
    void Transition(vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t mipLevel) const;

public:
    static void Transition(
        vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
        uint32_t baseMipLevel = 0, uint32_t mipLevels = 1
    );

private:
    vk::Image m_Handle { nullptr };
    VmaAllocation m_Allocation { nullptr };
    vk::ImageView m_View { nullptr };
    vk::Format m_Format = vk::Format::eUndefined;
    vk::Extent2D m_Extent = { 0, 0 };
    uint32_t m_MipLevels = 1;

    bool m_IsMoved = false;

private:
    [[nodiscard]] std::array<vk::Offset3D, 2> GetMipLevelArea(uint32_t level) const;
    [[nodiscard]] vk::ImageSubresourceLayers GetMipLayer(uint32_t level) const;

    void GenerateMips(vk::ImageLayout layout) const;

    static vk::AccessFlags GetAccessFlags(vk::ImageLayout layout);
    static vk::PipelineStageFlagBits GetPipelineStageFlags(vk::ImageLayout layout);
};

class ImageBuilder
{
public:
    ImageBuilder &SetFormat(vk::Format format);
    ImageBuilder &SetUsageFlags(vk::ImageUsageFlags usageFlags);
    ImageBuilder &EnableMips();

    ImageBuilder &ResetFlags();

    [[nodiscard]] Image CreateImage(vk::Extent2D extent) const;
    [[nodiscard]] Image CreateImage(vk::Extent2D extent, const std::string &name) const;
    [[nodiscard]] std::unique_ptr<Image> CreateImageUnique(vk::Extent2D extent) const;
    [[nodiscard]] std::unique_ptr<Image> CreateImageUnique(vk::Extent2D extent, const std::string &name)
        const;

private:
    vk::Format m_Format = vk::Format::eUndefined;
    vk::ImageUsageFlags m_UsageFlags;
    bool m_Mips = false;
};

}
