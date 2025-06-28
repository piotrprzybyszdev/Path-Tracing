#pragma once

#include <vulkan/vulkan.hpp>

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

    Image(Image &image) = delete;
    Image &operator=(Image &image) = delete;

    vk::Image GetHandle() const;
    vk::ImageView GetView() const;

private:
    vk::Image m_Handle { nullptr };
    vk::DeviceMemory m_Memory { nullptr };
    vk::ImageView m_View { nullptr };
};

class ImageBuilder
{
public:
    ImageBuilder &SetFormat(vk::Format format);
    ImageBuilder &SetUsageFlags(vk::ImageUsageFlags usageFlags);
    ImageBuilder &SetMemoryFlags(vk::MemoryPropertyFlags memoryFlags);

    ImageBuilder &ResetFlags();

    Image CreateImage(vk::Extent2D extent) const;
    std::unique_ptr<Image> CreateImageUnique(vk::Extent2D extent) const;

private:
    vk::Format m_Format = vk::Format::eUndefined;
    vk::ImageUsageFlags m_UsageFlags;
    vk::MemoryPropertyFlags m_MemoryFlags;
};

}
