#include <ranges>

#include "Core/Core.h"

#include "Application.h"
#include "DeviceContext.h"
#include "Renderer.h"
#include "ShaderBindingTable.h"

namespace PathTracing
{

ShaderBindingTable::ShaderBindingTable(uint32_t hitGroupCount)
    : m_HitGroupCount(hitGroupCount),
      m_HandleSize(DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleSize),
      m_HitGroupSize(m_HandleSize + sizeof(Shaders::SBTBuffer)),
      m_AlignedHandleSize(Utils::AlignTo(
          m_HandleSize, DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleAlignment
      )),
      m_AlignedHitGroupSize(Utils::AlignTo(
          m_HitGroupSize, DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleAlignment
      )),
      m_GroupBaseAlignment(DeviceContext::GetRayTracingPipelineProperties().shaderGroupBaseAlignment),
      m_HitRecordSize(m_AlignedHitGroupSize * m_HitGroupCount), m_RaygenRecordSize(m_HandleSize),
      m_RaygenTableSize(Utils::AlignTo(m_RaygenRecordSize, m_GroupBaseAlignment)),
      m_MissRecordSize(m_AlignedHandleSize),
      m_MissTableSize(Utils::AlignTo(m_MissRecordSize * m_HitGroupCount, m_GroupBaseAlignment))
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
void ShaderBindingTable::AddRecord(std::span<const SBTEntryInfo> entries)
{
    assert(entries.size() == m_HitGroupCount);

    if (m_ClosestHitRecordCount == m_ClosestHitRecordCapacity)
    {
        m_ClosestHitRecordCapacity = m_ClosestHitRecordCapacity == 0 ? 1 : m_ClosestHitRecordCapacity * 2;
        m_HitGroupRecords.reserve(m_ClosestHitRecordCapacity * m_HitRecordSize);
        m_HitGroupIndices.reserve(m_ClosestHitRecordCapacity * entries.size());
    }

    static_assert(Utils::uploadable<Shaders::SBTBuffer>);

    for (const SBTEntryInfo &entry : entries)
    {
        // Leave space for handle
        m_HitGroupRecords.resize(m_HitGroupRecords.size() + m_HandleSize);

        // Copy buffer
        const auto data = ToByteSpan(entry.Buffer);
        std::ranges::copy(data, std::back_inserter(m_HitGroupRecords));

        // Skip padding
        const uint32_t padding = m_AlignedHitGroupSize - m_HandleSize - sizeof(Shaders::SBTBuffer);
        m_HitGroupRecords.resize(m_HitGroupRecords.size() + padding);

        // Store the group indices
        m_HitGroupIndices.push_back(entry.HitGroupIndex);
        m_MaxShaderGroupIndex = std::max(m_MaxShaderGroupIndex, *std::ranges::max_element(m_HitGroupIndices));
    }

    m_ClosestHitRecordCount++;
    assert(m_ClosestHitRecordCount * m_HitRecordSize == m_HitGroupRecords.size());
}

void ShaderBindingTable::Upload(
    vk::Pipeline pipeline, uint32_t raygenIndex, std::span<const uint32_t> missGroupIndices
)
{
     m_MaxShaderGroupIndex = std::max(
        std::max(raygenIndex, *std::ranges::max_element(missGroupIndices)),
        *std::ranges::max_element(m_HitGroupIndices)
    );

    const uint32_t shaderGroupCount = m_MaxShaderGroupIndex + 1;
    m_ShaderHandles = DeviceContext::GetLogical().getRayTracingShaderGroupHandlesKHR<std::byte>(
        pipeline, 0, shaderGroupCount, m_AlignedHandleSize * shaderGroupCount,
        Application::GetDispatchLoader()
    );

    const uint32_t shaderBindingTableSize = m_RaygenTableSize + m_MissTableSize + m_HitGroupRecords.size();
    uint32_t offset = 0;

    auto builder = BufferBuilder().SetUsageFlags(vk::BufferUsageFlagBits::eTransferSrc);
    Buffer shaderBindingTable =
        builder.CreateHostBuffer(shaderBindingTableSize, "Shader Binding Table Staging Buffer");

    auto raygenHandle = std::span(m_ShaderHandles.begin() + raygenIndex * m_HandleSize, m_HandleSize);
    shaderBindingTable.Upload(raygenHandle, offset);
    offset += m_RaygenTableSize;

    for (int i = 0; i < missGroupIndices.size(); i++)
    {
        const uint32_t handleIndex = missGroupIndices[i];
        auto handle = std::span(m_ShaderHandles.begin() + handleIndex * m_HandleSize, m_HandleSize);
        shaderBindingTable.Upload(handle, offset);
        offset += m_MissRecordSize;
    }

    assert(offset == m_RaygenTableSize + m_MissRecordSize * missGroupIndices.size());

    offset = m_RaygenTableSize + m_MissTableSize;

    for (int i = 0; i < m_ClosestHitRecordCount; i++)
    {
        for (int j = 0; j < m_HitGroupCount; j++)
        {
            const uint32_t handleIndex = m_HitGroupIndices[i * m_HitGroupCount + j];
            auto handle = std::span(m_ShaderHandles.begin() + handleIndex * m_HandleSize, m_HandleSize);

            std::ranges::copy(
                handle, m_HitGroupRecords.begin() + i * m_HitRecordSize + j * m_AlignedHitGroupSize
            );
        }
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