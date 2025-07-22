#pragma once

#include <string>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

namespace PathTracing
{

class Image
{
public:
    Image(
        vk::Format format, vk::Extent2D extent, vk::ImageUsageFlags usageFlags,
        vk::MemoryPropertyFlags memoryFlags
    );

    ~Image();

    Image(Image &&image) noexcept;
    Image(const Image &image) = delete;

    Image &operator=(Image &&image) noexcept;
    Image &operator=(const Image &image) = delete;

    [[nodiscard]] vk::Image GetHandle() const;
    [[nodiscard]] vk::ImageView GetView() const;

    void UploadStaging(const uint8_t *data) const;

    void SetDebugName(const std::string &name) const;

    void Transition(vk::CommandBuffer buffer, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo) const;
    
public:
    static void Transition(
        vk::CommandBuffer buffer, vk::Image image, vk::ImageLayout layoutFrom, vk::ImageLayout layoutTo
    );

private:
    vk::Image m_Handle { nullptr };
    VmaAllocation m_Allocation { nullptr };
    vk::ImageView m_View { nullptr };
    vk::Format m_Format;
    vk::Extent2D m_Extent;

    bool m_IsMoved = false;

private:
    static vk::AccessFlags GetAccessFlags(vk::ImageLayout layout);
    static vk::PipelineStageFlagBits GetPipelineStageFlags(vk::ImageLayout layout);
};

class ImageBuilder
{
public:
    ImageBuilder &SetFormat(vk::Format format);
    ImageBuilder &SetUsageFlags(vk::ImageUsageFlags usageFlags);
    ImageBuilder &SetMemoryFlags(vk::MemoryPropertyFlags memoryFlags);

    ImageBuilder &ResetFlags();

    [[nodiscard]] Image CreateImage(vk::Extent2D extent) const;
    [[nodiscard]] Image CreateImage(vk::Extent2D extent, const std::string &name) const;
    [[nodiscard]] std::unique_ptr<Image> CreateImageUnique(vk::Extent2D extent) const;
    [[nodiscard]] std::unique_ptr<Image> CreateImageUnique(vk::Extent2D extent, const std::string &name) const;

private:
    vk::Format m_Format = vk::Format::eUndefined;
    vk::ImageUsageFlags m_UsageFlags;
    vk::MemoryPropertyFlags m_MemoryFlags;
};

}
