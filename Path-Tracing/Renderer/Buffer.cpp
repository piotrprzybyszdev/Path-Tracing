#include "Core/Core.h"

#include "Application.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "Utils.h"

namespace PathTracing
{

Buffer::Buffer(
    vk::BufferCreateFlags createFlags, vk::DeviceSize size, bool isDevice, vk::BufferUsageFlags usageFlags,
    vk::DeviceSize alignment
)
    : m_Size(size), m_IsDevice(isDevice)
{
    assert(size > 0);

    VkBufferCreateInfo createInfo = vk::BufferCreateInfo(createFlags, size, usageFlags);
    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = m_IsDevice ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    if (!m_IsDevice)
        allocinfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer handle = nullptr;

    VkResult result = vmaCreateBufferWithAlignment(
        DeviceContext::GetAllocator(), &createInfo, &allocinfo, alignment, &handle, &m_Allocation, nullptr
    );

    assert(result == VkResult::VK_SUCCESS);
    m_Handle = handle;

    VmaAllocationInfo info;
    vmaGetAllocationInfo(DeviceContext::GetAllocator(), m_Allocation, &info);
    assert(result == VkResult::VK_SUCCESS);

    VkMemoryPropertyFlags memoryProperties;
    vmaGetMemoryTypeProperties(DeviceContext::GetAllocator(), info.memoryType, &memoryProperties);

    if (!(memoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        m_IsDevice = false;
}

Buffer::~Buffer()
{
    if (m_IsMoved)
        return;

    vmaDestroyBuffer(DeviceContext::GetAllocator(), m_Handle, m_Allocation);
}

Buffer::Buffer(Buffer &&buffer) noexcept
    : m_Size(buffer.m_Size), m_Handle(buffer.m_Handle), m_Allocation(buffer.m_Allocation),
      m_IsDevice(buffer.m_IsDevice)
{
    buffer.m_IsMoved = true;
}

Buffer &Buffer::operator=(Buffer &&buffer) noexcept
{
    std::destroy_at(this);
    std::construct_at(this, std::move(buffer));
    return *this;
}

void Buffer::Upload(const void *data) const
{
    Upload(std::span(reinterpret_cast<const std::byte *>(data), m_Size));
}

void Buffer::Upload(BufferContent content, vk::DeviceSize offset) const
{
    assert(m_IsDevice == false);
    assert(m_Size >= content.Size);

    VkResult result = vmaCopyMemoryToAllocation(
        DeviceContext::GetAllocator(), content.Data, m_Allocation, offset, content.Size
    );
    assert(result == VkResult::VK_SUCCESS);
}

void Buffer::UploadStaging(vk::CommandBuffer commandBuffer, const Buffer &staging) const
{
    UploadStaging(commandBuffer, staging, staging.GetSize());
}

void Buffer::UploadStaging(vk::CommandBuffer commandBuffer, const Buffer &staging, vk::DeviceSize size) const
{
    assert(m_IsDevice == true);
    assert(staging.m_IsDevice == false);
    assert(staging.GetSize() >= size);
    assert(m_Size >= size);

    commandBuffer.copyBuffer(staging.GetHandle(), m_Handle, { vk::BufferCopy(0, 0, size) });
}

vk::Buffer Buffer::GetHandle() const
{
    return m_Handle;
}

vk::DeviceAddress Buffer::GetDeviceAddress() const
{
    return DeviceContext::GetLogical().getBufferAddress(vk::BufferDeviceAddressInfo(m_Handle));
}

vk::DeviceSize Buffer::GetSize() const
{
    return m_Size;
}

void Buffer::SetDebugName(const std::string &name) const
{
    Utils::SetDebugName(m_Handle, name);
}

bool Buffer::IsDevice() const
{
    return m_IsDevice;
}

BufferBuilder &BufferBuilder::SetCreateFlags(vk::BufferCreateFlags createFlags)
{
    m_CreateFlags = createFlags;
    return *this;
}

BufferBuilder &BufferBuilder::SetUsageFlags(vk::BufferUsageFlags usageFlags)
{
    m_UsageFlags = usageFlags;
    return *this;
}

BufferBuilder &BufferBuilder::SetAlignment(vk::DeviceSize alignment)
{
    m_Alignment = alignment;
    return *this;
}

BufferBuilder &BufferBuilder::ResetFlags()
{
    m_CreateFlags = vk::BufferCreateFlags();
    m_UsageFlags = vk::BufferUsageFlags();
    m_Alignment = 0;
    return *this;
}

Buffer BufferBuilder::CreateHostBuffer(vk::DeviceSize size) const
{
    return Buffer(m_CreateFlags, size, false, m_UsageFlags, m_Alignment);
}

Buffer BufferBuilder::CreateDeviceBuffer(vk::DeviceSize size) const
{
    auto buffer = Buffer(m_CreateFlags, size, true, m_UsageFlags, m_Alignment);
    if (!buffer.IsDevice())
        logger::warn("Unnamed buffer was allocated in RAM instead of VRAM");
    return buffer;
}

Buffer BufferBuilder::CreateHostBuffer(vk::DeviceSize size, const std::string &name) const
{
    Buffer buffer = CreateHostBuffer(size);
    buffer.SetDebugName(name);
    return buffer;
}

Buffer BufferBuilder::CreateDeviceBuffer(vk::DeviceSize size, const std::string &name) const
{
    Buffer buffer = CreateDeviceBuffer(size);
    buffer.SetDebugName(name);
    if (!buffer.IsDevice())
        logger::warn("Buffer {} was allocated in RAM instead of VRAM", name);
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateHostBufferUnique(vk::DeviceSize size) const
{
    return std::make_unique<Buffer>(m_CreateFlags, size, false, m_UsageFlags, m_Alignment);
}

std::unique_ptr<Buffer> BufferBuilder::CreateDeviceBufferUnique(vk::DeviceSize size) const
{
    auto buffer = std::make_unique<Buffer>(m_CreateFlags, size, true, m_UsageFlags, m_Alignment);
    if (!buffer->IsDevice())
        logger::warn("Unnamed buffer was allocated in RAM instead of VRAM");
    return std::move(buffer);
}

std::unique_ptr<Buffer> BufferBuilder::CreateHostBufferUnique(vk::DeviceSize size, const std::string &name)
    const
{
    auto buffer = CreateHostBufferUnique(size);
    buffer->SetDebugName(name);
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateDeviceBufferUnique(vk::DeviceSize size, const std::string &name)
    const
{
    auto buffer = CreateDeviceBufferUnique(size);
    buffer->SetDebugName(name);
    if (!buffer->IsDevice())
        logger::warn("Buffer {} was allocated in RAM instead of VRAM", name);
    return buffer;
}

Buffer BufferBuilder::CreateHostBuffer(BufferContent content) const
{
    Buffer buffer = CreateHostBuffer(content.Size);
    buffer.Upload(content.Data);
    return buffer;
}

Buffer BufferBuilder::CreateDeviceBuffer(vk::CommandBuffer commandBuffer, const Buffer &staging) const
{
    Buffer buffer = CreateDeviceBuffer(staging.GetSize());
    buffer.UploadStaging(commandBuffer, staging);
    if (!buffer.IsDevice())
        logger::warn("Unnamed buffer was allocated in RAM instead of VRAM");
    return buffer;
}

Buffer BufferBuilder::CreateHostBuffer(BufferContent content, const std::string &name) const
{
    Buffer buffer = CreateHostBuffer(content.Size, name);
    buffer.Upload(content.Data);
    return buffer;
}

Buffer BufferBuilder::CreateDeviceBuffer(
    vk::CommandBuffer commandBuffer, const Buffer &staging, const std::string &name
) const
{
    Buffer buffer = CreateDeviceBuffer(commandBuffer, staging);
    buffer.SetDebugName(name);
    if (!buffer.IsDevice())
        logger::warn("Buffer {} was allocated in RAM instead of VRAM", name);
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateHostBufferUnique(BufferContent content) const
{
    auto buffer = CreateHostBufferUnique(content.Size);
    buffer->Upload(content.Data);
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateDeviceBufferUnique(
    vk::CommandBuffer commandBuffer, const Buffer &staging
) const
{
    auto buffer = CreateDeviceBufferUnique(staging.GetSize());
    buffer->UploadStaging(commandBuffer, staging);
    if (!buffer->IsDevice())
        logger::warn("Unnamed buffer was allocated in RAM instead of VRAM");
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateHostBufferUnique(BufferContent content, const std::string &name)
    const
{
    auto buffer = CreateHostBufferUnique(content.Size, name);
    buffer->Upload(content.Data);
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateDeviceBufferUnique(
    vk::CommandBuffer commandBuffer, const Buffer &staging, const std::string &name
) const
{
    auto buffer = CreateDeviceBufferUnique(commandBuffer, staging);
    buffer->SetDebugName(name);
    if (!buffer->IsDevice())
        logger::warn("Buffer {} was allocated in RAM instead of VRAM", name);
    return buffer;
}

}
