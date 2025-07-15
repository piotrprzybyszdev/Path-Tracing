#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <memory>
#include <string_view>

namespace PathTracing
{

class Buffer
{
public:
    Buffer() = default;
    Buffer(
        vk::BufferCreateFlags createFlags, vk::DeviceSize size, vk::BufferUsageFlags usageFlags,
        vk::MemoryPropertyFlags memoryFlags, vk::MemoryAllocateFlags allocateFlags
    );
    ~Buffer();

    Buffer(Buffer &&buffer) noexcept;
    Buffer(const Buffer &buffer) = delete;

    Buffer &operator=(Buffer &&buffer) noexcept;
    Buffer &operator=(const Buffer &buffer) = delete;

    void Upload(const void *data) const;

    vk::Buffer GetHandle() const;
    vk::DeviceAddress GetDeviceAddress() const;
    vk::DeviceSize GetSize() const;

    void SetDebugName(std::string_view name) const;

private:
    vk::DeviceSize m_Size = 0;
    vk::Buffer m_Handle { nullptr };
    VmaAllocation m_Allocation { nullptr };

    bool m_IsDevice = false;

    bool m_IsMoved = false;
};

class BufferBuilder
{
public:
    BufferBuilder &SetCreateFlags(vk::BufferCreateFlags createFlags);
    BufferBuilder &SetUsageFlags(vk::BufferUsageFlags usageFlags);
    BufferBuilder &SetMemoryFlags(vk::MemoryPropertyFlags memoryFlags);
    BufferBuilder &SetAllocateFlags(vk::MemoryAllocateFlags allocateFlags);

    BufferBuilder &ResetFlags();

    Buffer CreateBuffer(vk::DeviceSize size) const;
    Buffer CreateBuffer(vk::DeviceSize size, std::string_view name) const;
    std::unique_ptr<Buffer> CreateBufferUnique(vk::DeviceSize size) const;
    std::unique_ptr<Buffer> CreateBufferUnique(vk::DeviceSize size, std::string_view name) const;

private:
    vk::BufferCreateFlags m_CreateFlags = {};
    vk::BufferUsageFlags m_UsageFlags = {};
    vk::MemoryPropertyFlags m_MemoryFlags = {};
    vk::MemoryAllocateFlags m_AllocateFlags = {};
};

}
