#include "Core/Core.h"

#include "Application.h"
#include "Buffer.h"
#include "DeviceContext.h"
#include "Utils.h"

namespace PathTracing
{

Buffer::Buffer(
    vk::BufferCreateFlags createFlags, vk::DeviceSize size, bool isDevice, vk::BufferUsageFlags usageFlags,
    vk::DeviceSize alignment, const std::string &name
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

    if (isDevice)
    {
        VmaAllocationInfo info;
        vmaGetAllocationInfo(DeviceContext::GetAllocator(), m_Allocation, &info);
        assert(result == VkResult::VK_SUCCESS);

        VkMemoryPropertyFlags memoryProperties;
        vmaGetMemoryTypeProperties(DeviceContext::GetAllocator(), info.memoryType, &memoryProperties);

        if (!(memoryProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            logger::warn("Buffer `{}` was allocated in RAM instead of VRAM", name);
            m_IsDevice = false;
        }
    }

    SetDebugName(name);
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
    assert(content.Data != nullptr);

    VkResult result = vmaCopyMemoryToAllocation(
        DeviceContext::GetAllocator(), content.Data, m_Allocation, offset, content.Size
    );
    assert(result == VkResult::VK_SUCCESS);
}

void Buffer::UploadStaging(vk::CommandBuffer commandBuffer, const Buffer &staging) const
{
    UploadStaging(commandBuffer, staging, m_Size);
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

vk::BufferMemoryBarrier2 Buffer::GetBarrier(vk::PipelineStageFlagBits2 src, vk::PipelineStageFlagBits2 dst)
    const
{
    return vk::BufferMemoryBarrier2(
        src, GetAccessFlagsSrc(src), dst, GetAccessFlagsDst(dst), vk::QueueFamilyIgnored,
        vk::QueueFamilyIgnored, m_Handle, 0, m_Size
    );
}

void Buffer::AddBarrier(
    vk::CommandBuffer commandBuffer, vk::PipelineStageFlagBits2 src, vk::PipelineStageFlagBits2 dst
) const
{
    vk::BufferMemoryBarrier2 barrier = GetBarrier(src, dst);
    vk::DependencyInfo info;
    info.setBufferMemoryBarriers(barrier);
    commandBuffer.pipelineBarrier2(info);
}

vk::AccessFlagBits2 Buffer::GetAccessFlagsSrc(vk::PipelineStageFlagBits2 stage)
{
    switch (stage)
    {
    case vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR:
        return vk::AccessFlagBits2::eAccelerationStructureWriteKHR;
    case vk::PipelineStageFlagBits2::eComputeShader:
        return vk::AccessFlagBits2::eShaderStorageWrite;
    default:
        throw error("Pipeline stage not supported");
    }
}

vk::AccessFlagBits2 Buffer::GetAccessFlagsDst(vk::PipelineStageFlagBits2 stage)
{
    switch (stage)
    {
    case vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR:
        return vk::AccessFlagBits2::eAccelerationStructureReadKHR;
    case vk::PipelineStageFlagBits2::eRayTracingShaderKHR:
        return vk::AccessFlagBits2::eAccelerationStructureReadKHR;
    default:
        throw error("Pipeline stage not supported");
    }
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

Buffer BufferBuilder::CreateHostBuffer(vk::DeviceSize size, const std::string &name) const
{
    return Buffer(m_CreateFlags, size, false, m_UsageFlags, m_Alignment, name);
}

Buffer BufferBuilder::CreateDeviceBuffer(vk::DeviceSize size, const std::string &name) const
{
    return Buffer(m_CreateFlags, size, true, m_UsageFlags, m_Alignment, name);
}

std::unique_ptr<Buffer> BufferBuilder::CreateHostBufferUnique(vk::DeviceSize size, const std::string &name)
    const
{
    return std::make_unique<Buffer>(m_CreateFlags, size, false, m_UsageFlags, m_Alignment, name);
}

std::unique_ptr<Buffer> BufferBuilder::CreateDeviceBufferUnique(vk::DeviceSize size, const std::string &name)
    const
{
    return std::make_unique<Buffer>(m_CreateFlags, size, true, m_UsageFlags, m_Alignment, name);
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
    Buffer buffer = CreateDeviceBuffer(staging.GetSize(), name);
    buffer.UploadStaging(commandBuffer, staging);
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
    auto buffer = CreateDeviceBufferUnique(staging.GetSize(), name);
    buffer->UploadStaging(commandBuffer, staging);
    return buffer;
}

}
