#pragma once

#include <vulkan/vulkan.hpp>

namespace PathTracing
{

class PhysicalDevice;

class Image
{
public:
    Image(
        vk::Device device, const PhysicalDevice &physicalDevice, vk::Format format, uint32_t width,
        uint32_t height, vk::ImageUsageFlags usageFlags, vk::MemoryPropertyFlags memoryFlags
    );

    ~Image();

    Image(Image &image) = delete;
    Image &operator=(Image &image) = delete;

    vk::Image GetHandle() const;
    vk::ImageView GetView() const;

private:
    vk::Device m_Device { nullptr };

    uint32_t m_Width = 0, m_Height = 0;
    vk::Format m_Format;

    vk::Image m_Handle { nullptr };
    vk::DeviceMemory m_Memory { nullptr };
    vk::ImageView m_View { nullptr };
};

class ImageBuilder
{
public:
    ImageBuilder(vk::Device device, const PhysicalDevice &physicalDevice);

    ImageBuilder &SetFormat(vk::Format format);
    ImageBuilder &SetUsageFlags(vk::ImageUsageFlags usageFlags);
    ImageBuilder &SetMemoryFlags(vk::MemoryPropertyFlags memoryFlags);

    ImageBuilder &ResetFlags();

    Image CreateImage(uint32_t width, uint32_t height) const;
    std::unique_ptr<Image> CreateImageUnique(uint32_t width, uint32_t height) const;

private:
    vk::Device m_Device;
    const PhysicalDevice &m_PhysicalDevice;

    vk::Format m_Format = vk::Format::eUndefined;
    vk::ImageUsageFlags m_UsageFlags;
    vk::MemoryPropertyFlags m_MemoryFlags;
};

}
