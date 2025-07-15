#include "Core/Core.h"

#include "Application.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "Utils.h"

namespace PathTracing
{

Buffer::Buffer(
    vk::BufferCreateFlags createFlags, vk::DeviceSize size, vk::BufferUsageFlags usageFlags,
    vk::MemoryPropertyFlags memoryFlags, vk::MemoryAllocateFlags allocateFlags
)
    : m_Size(size), m_IsDevice(memoryFlags & vk::MemoryPropertyFlagBits::eDeviceLocal)
{
    VkBufferCreateInfo createInfo = vk::BufferCreateInfo(createFlags, size, usageFlags);
    VmaAllocationCreateInfo allocinfo = {};
    allocinfo.usage = m_IsDevice ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    if (!m_IsDevice)
        allocinfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

    VkBuffer buffer = nullptr;
    VkResult result = vmaCreateBuffer(DeviceContext::GetAllocator(), &createInfo, &allocinfo, &buffer, &m_Allocation, nullptr);
    assert(result == VkResult::VK_SUCCESS);
    m_Handle = buffer;
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

    BufferBuilder builder;
    builder.SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc)
        .SetMemoryFlags(vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

    Buffer staging = builder.CreateBuffer(m_Size);
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

void Buffer::SetDebugName(std::string_view name) const
{
    Utils::SetDebugName(m_Handle, vk::ObjectType::eBuffer, name);
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

BufferBuilder &BufferBuilder::SetMemoryFlags(vk::MemoryPropertyFlags memoryFlags)
{
    m_MemoryFlags = memoryFlags;
    return *this;
}

BufferBuilder &BufferBuilder::SetAllocateFlags(vk::MemoryAllocateFlags allocateFlags)
{
    m_AllocateFlags = allocateFlags;
    return *this;
}

BufferBuilder &BufferBuilder::ResetFlags()
{
    m_CreateFlags = vk::BufferCreateFlags();
    m_UsageFlags = vk::BufferUsageFlags();
    m_MemoryFlags = vk::MemoryPropertyFlags();
    m_AllocateFlags = vk::MemoryAllocateFlags();
    return *this;
}

Buffer BufferBuilder::CreateBuffer(vk::DeviceSize size) const
{
    return Buffer(m_CreateFlags, size, m_UsageFlags, m_MemoryFlags, m_AllocateFlags);
}

Buffer BufferBuilder::CreateBuffer(vk::DeviceSize size, std::string_view name) const
{
    Buffer buffer = CreateBuffer(size);
    buffer.SetDebugName(name);
    return buffer;
}

std::unique_ptr<Buffer> BufferBuilder::CreateBufferUnique(vk::DeviceSize size) const
{
    return std::make_unique<Buffer>(m_CreateFlags, size, m_UsageFlags, m_MemoryFlags, m_AllocateFlags);
}

std::unique_ptr<Buffer> BufferBuilder::CreateBufferUnique(vk::DeviceSize size, std::string_view name) const
{
    auto buffer = CreateBufferUnique(size);
    buffer->SetDebugName(name);
    return buffer;
}

}
