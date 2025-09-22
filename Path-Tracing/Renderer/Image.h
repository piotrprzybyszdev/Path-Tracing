#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <span>
#include <string>

#include "Buffer.h"

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
    Image(
        vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags, uint32_t layers, bool mips,
        bool isCube
    );

    ~Image();

    Image(Image &&image) noexcept;
    Image(const Image &image) = delete;

    Image &operator=(Image &&image) noexcept;
    Image &operator=(const Image &image) = delete;

    [[nodiscard]] vk::Extent2D GetExtent() const;
    [[nodiscard]] vk::Image GetHandle() const;
    [[nodiscard]] vk::ImageView GetView() const;
    [[nodiscard]] vk::Format GetFormat() const;

    /* Commands from the transfer buffer should be executed before mip buffer */
    /* If they are the same buffer they can be submitted at once because of barriers*/
    void UploadStaging(
        vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const Buffer &buffer,
        const Image &temporary, vk::Extent2D extent, vk::ImageLayout layout
    ) const;
    void UploadStaging(
        vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const Buffer &buffer,
        const Image &temporary, vk::Extent2D extent, vk::ImageLayout layout, uint32_t layer,
        uint32_t layerCount
    ) const;

    void SetDebugName(const std::string &name) const;

    void Transition(
        vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t layer = 0,
        uint32_t layerCount = 1
    ) const;
    void TransitionMip(
        vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo, uint32_t mipLevel,
        uint32_t layer = 0, uint32_t layerCount = 1
    ) const;

public:
    [[nodiscard]] static vk::DeviceSize GetByteSize(
        vk::Extent2D extent, vk::Format format, uint32_t layers = 1
    );

    static void Transition(
        vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo,
        uint32_t baseMipLevel = 0, uint32_t mipLevels = 1, uint32_t layer = 0, uint32_t layerCount = 1
    );

    static std::array<vk::Offset3D, 2> GetMipLevelArea(vk::Extent2D extent, uint32_t level = 0);

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
    [[nodiscard]] std::array<vk::Offset3D, 2> GetMipLevelArea(uint32_t level = 0) const;
    [[nodiscard]] vk::ImageSubresourceLayers GetMipLayer(
        uint32_t level, uint32_t layer = 0, uint32_t layerCount = 1
    ) const;

    void UploadFromBuffer(
        vk::CommandBuffer commandBuffer, const Buffer &buffer, vk::Extent2D extent, uint32_t mip,
        uint32_t layer = 0, uint32_t layerCount = 1
    ) const;

    [[nodiscard]] uint32_t GetMip(vk::Extent2D extent) const;
    void Scale(
        vk::CommandBuffer mipBuffer, vk::CommandBuffer transferBuffer, const Buffer &buffer,
        vk::Extent2D extent, uint32_t destMip
    ) const;
    void GenerateFullMips(
        vk::CommandBuffer commandBuffer, vk::ImageLayout layout, uint32_t layer = 0, uint32_t layerCount = 1
    ) const;
    void GenerateMips(
        vk::CommandBuffer commandBuffer, vk::ImageLayout layout, uint32_t fromMip, uint32_t toMip,
        uint32_t layer = 0, uint32_t layerCount = 1
    ) const;

    static vk::AccessFlags2 GetAccessFlags(vk::ImageLayout layout);
    static vk::PipelineStageFlags2 GetPipelineStageFlags(vk::ImageLayout layout);
};

class ImageBuilder
{
public:
    ImageBuilder &SetFormat(vk::Format format);
    ImageBuilder &SetUsageFlags(vk::ImageUsageFlags usageFlags);
    ImageBuilder &EnableMips(bool value = true);
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
