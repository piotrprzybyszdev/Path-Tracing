#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <span>
#include <string>

#include "Utils.h"

namespace PathTracing
{

struct BufferContent
{
    template<Utils::uploadable T>
    BufferContent(std::span<T> content) : Size(content.size_bytes()), Data(content.data())
    {
    }

    vk::DeviceSize Size;
    const void *Data;
};

class Buffer
{
public:
    Buffer() = default;
    Buffer(
        vk::BufferCreateFlags createFlags, vk::DeviceSize size, bool isDevice,
        vk::BufferUsageFlags usageFlags, vk::DeviceSize alignment
    );
    ~Buffer();

    Buffer(Buffer &&buffer) noexcept;
    Buffer(const Buffer &buffer) = delete;

    Buffer &operator=(Buffer &&buffer) noexcept;
    Buffer &operator=(const Buffer &buffer) = delete;

    void Upload(const void *data) const;
    void Upload(BufferContent content, vk::DeviceSize offset = 0) const;

    void UploadStaging(vk::CommandBuffer commandBuffer, const Buffer &staging) const;
    void UploadStaging(vk::CommandBuffer commandBuffer, const Buffer &staging, vk::DeviceSize size) const;

    [[nodiscard]] vk::Buffer GetHandle() const;
    [[nodiscard]] vk::DeviceAddress GetDeviceAddress() const;
    [[nodiscard]] vk::DeviceSize GetSize() const;

    void SetDebugName(const std::string &name) const;

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
    BufferBuilder &SetAlignment(vk::DeviceSize alignment);

    BufferBuilder &ResetFlags();

    [[nodiscard]] Buffer CreateHostBuffer(vk::DeviceSize size) const;
    [[nodiscard]] Buffer CreateDeviceBuffer(vk::DeviceSize size) const;
    [[nodiscard]] Buffer CreateHostBuffer(vk::DeviceSize size, const std::string &name) const;
    [[nodiscard]] Buffer CreateDeviceBuffer(vk::DeviceSize size, const std::string &name) const;
    [[nodiscard]] std::unique_ptr<Buffer> CreateHostBufferUnique(vk::DeviceSize size) const;
    [[nodiscard]] std::unique_ptr<Buffer> CreateDeviceBufferUnique(vk::DeviceSize size) const;
    [[nodiscard]] std::unique_ptr<Buffer> CreateHostBufferUnique(vk::DeviceSize size, const std::string &name)
        const;
    [[nodiscard]] std::unique_ptr<Buffer> CreateDeviceBufferUnique(
        vk::DeviceSize size, const std::string &name
    ) const;

    [[nodiscard]] Buffer CreateHostBuffer(BufferContent content) const;
    [[nodiscard]] Buffer CreateDeviceBuffer(vk::CommandBuffer commandBuffer, const Buffer &staging) const;
    [[nodiscard]] Buffer CreateHostBuffer(BufferContent content, const std::string &name) const;
    [[nodiscard]] Buffer CreateDeviceBuffer(
        vk::CommandBuffer commandBuffer, const Buffer &staging, const std::string &name
    ) const;
    [[nodiscard]] std::unique_ptr<Buffer> CreateHostBufferUnique(BufferContent content) const;
    [[nodiscard]] std::unique_ptr<Buffer> CreateDeviceBufferUnique(
        vk::CommandBuffer commandBuffer, const Buffer &staging
    ) const;
    [[nodiscard]] std::unique_ptr<Buffer> CreateHostBufferUnique(
        BufferContent content, const std::string &name
    ) const;
    [[nodiscard]] std::unique_ptr<Buffer> CreateDeviceBufferUnique(
        vk::CommandBuffer commandBuffer, const Buffer &staging, const std::string &name
    ) const;

private:
    vk::BufferCreateFlags m_CreateFlags;
    vk::BufferUsageFlags m_UsageFlags;
    vk::DeviceSize m_Alignment = 0;
};

}
