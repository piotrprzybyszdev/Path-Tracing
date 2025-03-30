#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>

namespace PathTracing
{

class PhysicalDevice;

class Buffer
{
public:
    Buffer() = default;
    Buffer(
        vk::Device device, const PhysicalDevice &physicalDevice, vk::BufferCreateFlags createFlags,
        vk::DeviceSize size, vk::BufferUsageFlags usageFlags, vk::MemoryPropertyFlags memoryFlags,
        vk::MemoryAllocateFlags allocateFlags
    );
    ~Buffer();

    Buffer(Buffer &buffer) = delete;
    Buffer &operator=(Buffer &buffer) = delete;

    Buffer &operator=(Buffer &&buffer) noexcept;

    void Upload(const void *data);

    vk::Buffer GetHandle() const;
    vk::DeviceAddress GetDeviceAddress() const;
    vk::DeviceSize GetSize() const;

private:
    vk::Device m_Device { nullptr };

    vk::DeviceSize m_Size = 0;
    vk::Buffer m_Handle { nullptr };
    vk::DeviceMemory m_Memory { nullptr };

    bool m_IsMoved = false;
};

class BufferBuilder
{
public:
    BufferBuilder(vk::Device device, const PhysicalDevice &physicalDevice);

    BufferBuilder &SetCreateFlags(vk::BufferCreateFlags createFlags);
    BufferBuilder &SetUsageFlags(vk::BufferUsageFlags usageFlags);
    BufferBuilder &SetMemoryFlags(vk::MemoryPropertyFlags memoryFlags);
    BufferBuilder &SetAllocateFlags(vk::MemoryAllocateFlags allocateFlags);

    BufferBuilder &ResetFlags();

    Buffer CreateBuffer(vk::DeviceSize size) const;
    std::unique_ptr<Buffer> CreateBufferUnique(vk::DeviceSize size) const;

private:
    vk::Device m_Device { nullptr };
    const PhysicalDevice &m_PhysicalDevice;

    vk::BufferCreateFlags m_CreateFlags = {};
    vk::BufferUsageFlags m_UsageFlags = {};
    vk::MemoryPropertyFlags m_MemoryFlags = {};
    vk::MemoryAllocateFlags m_AllocateFlags = {};
};

}
