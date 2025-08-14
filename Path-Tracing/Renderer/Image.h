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
    Image(
        vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t layers,
        uint32_t mipLevels, bool isCube
    );
    Image(vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t layers, bool mips, bool isCube);

    ~Image();

    Image(Image &&image) noexcept;
    Image(const Image &image) = delete;

    Image &operator=(Image &&image) noexcept;
    Image &operator=(const Image &image) = delete;

    [[nodiscard]] vk::Image GetHandle() const;
    [[nodiscard]] vk::ImageView GetView() const;

    void UploadStaging(const std::byte *data, vk::Extent2D extent, vk::ImageLayout layout, uint32_t layer = 0)
        const;

    void SetDebugName(const std::string &name) const;

    void Transition(
        vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t layer = 0
    ) const;
    void TransitionMip(
        vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t mipLevel,
        uint32_t layer = 0
    ) const;

public:
    static void Transition(
        vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
        uint32_t baseMipLevel = 0, uint32_t mipLevels = 1, uint32_t layer = 0
    );

private:
    vk::Image m_Handle { nullptr };
    VmaAllocation m_Allocation { nullptr };
    vk::ImageView m_View { nullptr };
    vk::Format m_Format = vk::Format::eUndefined;
    vk::Extent2D m_Extent = { 0, 0 };
    uint32_t m_MipLevels = 1;
    uint32_t m_Layers = 1;

    bool m_IsMoved = false;

private:
    [[nodiscard]] std::array<vk::Offset3D, 2> GetMipLevelArea(uint32_t level) const;
    [[nodiscard]] vk::ImageSubresourceLayers GetMipLayer(uint32_t level, uint32_t layer = 0) const;

    void GenerateMips(uint32_t layer, vk::ImageLayout layout) const;

    static vk::AccessFlags GetAccessFlags(vk::ImageLayout layout);
    static vk::PipelineStageFlagBits GetPipelineStageFlags(vk::ImageLayout layout);
};

class ImageBuilder
{
public:
    ImageBuilder &SetFormat(vk::Format format);
    ImageBuilder &SetUsageFlags(vk::ImageUsageFlags usageFlags);
    ImageBuilder &EnableMips();
    ImageBuilder &SetLayers(uint32_t layers);
    ImageBuilder &EnableCube();

    ImageBuilder &ResetFlags();

    [[nodiscard]] Image CreateImage(vk::Extent2D extent) const;
    [[nodiscard]] Image CreateImage(vk::Extent2D extent, const std::string &name) const;
    [[nodiscard]] std::unique_ptr<Image> CreateImageUnique(vk::Extent2D extent) const;
    [[nodiscard]] std::unique_ptr<Image> CreateImageUnique(vk::Extent2D extent, const std::string &name)
        const;

private:
    vk::Format m_Format = vk::Format::eUndefined;
    vk::ImageUsageFlags m_UsageFlags;
    uint32_t m_Layers = 1;
    bool m_Mips = false;
    bool m_Cube = false;
};

}
