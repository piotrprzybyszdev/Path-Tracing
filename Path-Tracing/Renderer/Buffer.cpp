#include "Core/Core.h"

#include "Application.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "Utils.h"

namespace PathTracing
{

Buffer::Buffer(
    vk::BufferCreateFlags createFlags, vk::DeviceSize size, bool isDevice, vk::BufferUsageFlags usageFlags, vk::DeviceSize alignment
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
}

Buffer::~Buffer()
{
    if (m_IsMoved)
        return;

    vmaDestroyBuffer(DeviceContext::GetAllocator(), m_Handle, m_Allocation);
}

Buffer::Buffer(Buffer &&buffer) noexcept
{
    m_Size = buffer.m_Size;
    m_Handle = buffer.m_Handle;
    m_Allocation = buffer.m_Allocation;

    buffer.m_IsMoved = true;
}

Buffer &Buffer::operator=(Buffer &&buffer) noexcept
{
    if (m_Handle != nullptr)
        this->~Buffer();

    static_assert(!std::is_polymorphic_v<Buffer>);
    new (this) Buffer(std::move(buffer));

    return *this;
}

void Buffer::Upload(const void *data) const
{
    if (!m_IsDevice)
    {
        void *dst = nullptr;
        VkResult result = vmaMapMemory(DeviceContext::GetAllocator(), m_Allocation, &dst);
        assert(result == VkResult::VK_SUCCESS);
        memcpy(dst, data, m_Size);
        vmaUnmapMemory(DeviceContext::GetAllocator(), m_Allocation);
        return;
    }

    Buffer staging =
        BufferBuilder().SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc).CreateHostBuffer(m_Size);
    staging.Upload(data);

    Renderer::s_MainCommandBuffer.Begin();
    Renderer::s_MainCommandBuffer.CommandBuffer.copyBuffer(
        staging.GetHandle(), m_Handle, { vk::BufferCopy(0, 0, m_Size) }
    );
    Renderer::s_MainCommandBuffer.Submit(DeviceContext::GetGraphicsQueue());
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
    return Buffer(m_CreateFlags, size, true, m_UsageFlags, m_Alignment);
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
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateHostBufferUnique(vk::DeviceSize size) const
{
    return std::make_unique<Buffer>(m_CreateFlags, size, false, m_UsageFlags, m_Alignment);
}

std::unique_ptr<Buffer> BufferBuilder::CreateDeviceBufferUnique(vk::DeviceSize size) const
{
    return std::make_unique<Buffer>(m_CreateFlags, size, true, m_UsageFlags, m_Alignment);
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
    return buffer;
}

Buffer BufferBuilder::CreateHostBuffer(BufferContent content) const
{
    Buffer buffer = CreateHostBuffer(content.Size);
    buffer.Upload(content.Data);
    return buffer;
}

Buffer BufferBuilder::CreateDeviceBuffer(BufferContent content) const
{
    Buffer buffer = CreateDeviceBuffer(content.Size);
    buffer.Upload(content.Data);
    return buffer;
}

Buffer BufferBuilder::CreateHostBuffer(BufferContent content, const std::string &name) const
{
    Buffer buffer = CreateHostBuffer(content.Size, name);
    buffer.Upload(content.Data);
    return buffer;
}

Buffer BufferBuilder::CreateDeviceBuffer(BufferContent content, const std::string &name) const
{
    Buffer buffer = CreateDeviceBuffer(content.Size, name);
    buffer.Upload(content.Data);
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateHostBufferUnique(BufferContent content) const
{
    auto buffer = CreateHostBufferUnique(content.Size);
    buffer->Upload(content.Data);
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateDeviceBufferUnique(BufferContent content) const
{
    auto buffer = CreateDeviceBufferUnique(content.Size);
    buffer->Upload(content.Data);
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
    BufferContent content, const std::string &name
) const
{
    auto buffer = CreateDeviceBufferUnique(content.Size, name);
    buffer->Upload(content.Data);
    return buffer;
}

}
