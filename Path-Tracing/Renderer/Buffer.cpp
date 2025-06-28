#include "Core/Core.h"

#include "Buffer.h"
#include "DeviceContext.h"

namespace PathTracing
{

Buffer::Buffer(
    vk::BufferCreateFlags createFlags, vk::DeviceSize size, vk::BufferUsageFlags usageFlags,
    vk::MemoryPropertyFlags memoryFlags, vk::MemoryAllocateFlags allocateFlags
)
    : m_Size(size)
{
    vk::BufferCreateInfo createInfo(createFlags, size, usageFlags);
    m_Handle = DeviceContext::GetLogical().createBuffer(createInfo);
    vk::MemoryRequirements requirements = DeviceContext::GetLogical().getBufferMemoryRequirements(m_Handle);

    uint32_t memoryTypeIndex = DeviceContext::FindMemoryTypeIndex(requirements, memoryFlags);

    vk::MemoryAllocateFlagsInfo allocateFlagsInfo(allocateFlags);
    vk::MemoryAllocateInfo allocateInfo(requirements.size, memoryTypeIndex, &allocateFlagsInfo);
    m_Memory = DeviceContext::GetLogical().allocateMemory(allocateInfo);
    DeviceContext::GetLogical().bindBufferMemory(m_Handle, m_Memory, 0);
}

Buffer &Buffer::operator=(Buffer &&buffer) noexcept
{
    if (m_Size != 0)
        this->~Buffer();

    m_Size = buffer.m_Size;
    m_Handle = buffer.m_Handle;
    m_Memory = buffer.m_Memory;

    buffer.m_IsMoved = true;

    return *this;
}

Buffer::~Buffer()
{
    if (m_IsMoved)
        return;

    DeviceContext::GetLogical().destroyBuffer(m_Handle);
    DeviceContext::GetLogical().freeMemory(m_Memory);
}

void Buffer::Upload(const void *data) const
{
    void *dst = DeviceContext::GetLogical().mapMemory(m_Memory, 0, m_Size);
    memcpy(dst, data, m_Size);
    DeviceContext::GetLogical().unmapMemory(m_Memory);
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

std::unique_ptr<Buffer> BufferBuilder::CreateBufferUnique(vk::DeviceSize size) const
{
    return std::make_unique<Buffer>(m_CreateFlags, size, m_UsageFlags, m_MemoryFlags, m_AllocateFlags);
}

}
