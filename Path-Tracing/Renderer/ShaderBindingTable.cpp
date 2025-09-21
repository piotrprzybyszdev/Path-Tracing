#include <ranges>

#include "Core/Core.h"

#include "Application.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "ShaderBindingTable.h"

namespace PathTracing
{

ShaderBindingTable::ShaderBindingTable(
    uint32_t raygenGroupIndex, std::vector<uint32_t> &&missGroupIndices,
    std::vector<uint32_t> &&hitGroupIndices
)
    : m_RaygenGroupIndex(raygenGroupIndex), m_MissGroupIndices(std::move(missGroupIndices)),
      m_HitGroupIndices(std::move(hitGroupIndices)),
      m_HandleSize(DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleSize),
      m_HitGroupSize(m_HandleSize + sizeof(Shaders::SBTBuffer)),
      m_AlignedHandleSize(Utils::AlignTo(
          m_HandleSize, DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleAlignment
      )),
      m_AlignedHitGroupSize(Utils::AlignTo(
          m_HitGroupSize, DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleAlignment
      )),
      m_GroupBaseAlignment(DeviceContext::GetRayTracingPipelineProperties().shaderGroupBaseAlignment),
      m_HitRecordSize(m_AlignedHitGroupSize * m_HitGroupIndices.size()), m_RaygenRecordSize(m_HandleSize),
      m_RaygenTableSize(Utils::AlignTo(m_RaygenRecordSize, m_GroupBaseAlignment)),
      m_MissRecordSize(m_AlignedHandleSize),
      m_MissTableSize(Utils::AlignTo(m_MissRecordSize * m_MissGroupIndices.size(), m_GroupBaseAlignment))
{
    assert(
        sizeof(Shaders::SBTBuffer) <= DeviceContext::GetRayTracingPipelineProperties().maxRayHitAttributeSize
    );
    assert(m_HitGroupSize <= DeviceContext::GetRayTracingPipelineProperties().maxShaderGroupStride);
    assert(m_HandleSize % alignof(Shaders::SBTBuffer) == 0);

    logger::debug("Handle size: {}", m_HandleSize);
    logger::debug("Hit Group size: {}", m_HitGroupSize);
    logger::debug("Aligned Handle size: {}", m_AlignedHandleSize);
    logger::debug("Aligned Hit Group size: {}", m_AlignedHitGroupSize);
}

ShaderBindingTable::~ShaderBindingTable() = default;

/*
 *  Hit Table layout:
 *                           hit record                         |
 *  -------------------------------------------------------------
 *     aligned hit group size    |   aligned hit group size     |
 *  ------------------------------------------------------------|
 *  Handle | SBTBuffer | padding | Handle | SBTBuffer | padding |
 *  ------------------------------------------------------------|
 *       (eg. primary ray)       |      (eg. occulsion ray)
 */
void ShaderBindingTable::AddRecord(std::span<const Shaders::SBTBuffer> buffers)
{
    assert(buffers.size() == m_HitGroupIndices.size());

    if (m_ClosestHitRecordCount == m_ClosestHitRecordCapacity)
    {
        m_ClosestHitRecordCapacity = m_ClosestHitRecordCapacity == 0 ? 1 : m_ClosestHitRecordCapacity * 2;
        m_HitGroupRecords.reserve(m_ClosestHitRecordCapacity * m_HitRecordSize);
    }

    static_assert(Utils::uploadable<Shaders::SBTBuffer>);

    for (const Shaders::SBTBuffer &data : buffers)
    {
        // Leave space for handle
        m_HitGroupRecords.resize(m_HitGroupRecords.size() + m_HandleSize);

        // Copy buffer
        const auto *ptr = reinterpret_cast<const std::byte *>(&data);
        std::copy(ptr, ptr + sizeof(Shaders::SBTBuffer), std::back_inserter(m_HitGroupRecords));

        // Skip padding
        const uint32_t padding = m_AlignedHitGroupSize - m_HandleSize - sizeof(Shaders::SBTBuffer);
        m_HitGroupRecords.resize(m_HitGroupRecords.size() + padding);
    }

    m_ClosestHitRecordCount++;
    assert(m_ClosestHitRecordCount * m_HitRecordSize == m_HitGroupRecords.size());
}

void ShaderBindingTable::Upload(vk::Pipeline pipeline)
{
    const uint32_t shaderGroupCount = 1 + m_MissGroupIndices.size() + m_HitGroupIndices.size();
    m_ShaderHandles = DeviceContext::GetLogical().getRayTracingShaderGroupHandlesKHR<std::byte>(
        pipeline, 0, shaderGroupCount, m_AlignedHandleSize * shaderGroupCount,
        Application::GetDispatchLoader()
    );

    const uint32_t shaderBindingTableSize = m_RaygenTableSize + m_MissTableSize + m_HitGroupRecords.size();
    uint32_t offset = 0;

    auto builder = BufferBuilder().SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc);
    Buffer shaderBindingTable =
        builder.CreateHostBuffer(shaderBindingTableSize, "Shader Binding Table Staging Buffer");

    auto raygenHandle = std::span(m_ShaderHandles.begin() + m_RaygenGroupIndex * m_HandleSize, m_HandleSize);
    shaderBindingTable.Upload(raygenHandle, offset);
    offset += m_RaygenTableSize;

    for (int i = 0; i < m_MissGroupIndices.size(); i++)
    {
        const uint32_t handleIndex = m_MissGroupIndices[i];
        auto handle = std::span(m_ShaderHandles.begin() + handleIndex * m_HandleSize, m_HandleSize);
        shaderBindingTable.Upload(handle, offset);
        offset += m_MissRecordSize;
    }

    assert(offset == m_RaygenTableSize + m_MissRecordSize * m_MissGroupIndices.size());

    offset = m_RaygenTableSize + m_MissTableSize;

    for (int i = 0; i < m_HitGroupIndices.size(); i++)
    {
        const uint32_t handleIndex = m_HitGroupIndices[i];
        auto handle = std::span(m_ShaderHandles.begin() + handleIndex * m_HandleSize, m_HandleSize);

        const uint32_t hitRecordSize = m_HitGroupIndices.size() * m_AlignedHitGroupSize;
        for (int j = 0; j < m_ClosestHitRecordCount; j++)
            std::ranges::copy(
                handle, m_HitGroupRecords.begin() + j * hitRecordSize + i * m_AlignedHitGroupSize
            );
    }
    
    shaderBindingTable.Upload(std::span(m_HitGroupRecords), offset);

    builder
        .SetUsageFlags(
            vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderBindingTableKHR |
            vk::BufferUsageFlagBits::eShaderDeviceAddress
        )
        .SetAlignment(m_GroupBaseAlignment);

    Renderer::s_MainCommandBuffer->Begin();
    m_TableBuffer = builder.CreateDeviceBuffer(
        Renderer::s_MainCommandBuffer->Buffer, shaderBindingTable, "Shader Binding Table Buffer"
    );
    Renderer::s_MainCommandBuffer->SubmitBlocking();

    m_TableBufferDeviceAddress = m_TableBuffer.GetDeviceAddress();
}

vk::StridedDeviceAddressRegionKHR ShaderBindingTable::GetRaygenTableEntry() const
{
    return { m_TableBufferDeviceAddress, m_AlignedHandleSize, m_AlignedHandleSize };
}

vk::StridedDeviceAddressRegionKHR ShaderBindingTable::GetMissTableEntry() const
{
    return { m_TableBufferDeviceAddress + m_RaygenTableSize, m_MissRecordSize, m_MissRecordSize };
}

vk::StridedDeviceAddressRegionKHR ShaderBindingTable::GetClosestHitTableEntry() const
{
    return { m_TableBufferDeviceAddress + m_RaygenTableSize + m_MissTableSize, m_AlignedHitGroupSize,
             m_AlignedHitGroupSize };
}

}