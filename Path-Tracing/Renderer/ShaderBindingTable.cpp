#include <ranges>

#include "Core/Core.h"

#include "Application.h"

#include "DeviceContext.h"
#include "ShaderBindingTable.h"

namespace PathTracing
{

ShaderBindingTable::ShaderBindingTable()
    : m_HandleSize(DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleSize),
      m_HitGroupSize(m_HandleSize + sizeof(Shaders::SBTBuffer)),
      m_AlignedHandleSize(
          Utils::AlignTo(m_HandleSize, DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleAlignment)
      ),
      m_AlignedHitGroupSize(
          Utils::AlignTo(m_HitGroupSize, DeviceContext::GetRayTracingPipelineProperties().shaderGroupHandleAlignment)
      ),
      m_GroupBaseAlignment(DeviceContext::GetRayTracingPipelineProperties().shaderGroupBaseAlignment)
{
    assert(m_HitGroupSize < DeviceContext::GetRayTracingPipelineProperties().maxShaderGroupStride);
    assert(m_HandleSize % alignof(Shaders::SBTBuffer) == 0);

    logger::debug("Handle size: {}", m_HandleSize);
    logger::debug("Hit Group size: {}", m_HitGroupSize);
    logger::debug("Aligned Handle size: {}", m_AlignedHandleSize);
    logger::debug("Aligned Hit Group size: {}", m_AlignedHitGroupSize);
}

ShaderBindingTable::~ShaderBindingTable() = default;

/*
 *  SBT layout:
 *     aligned hit group size    |
 *  -----------------------------------------------------------
 *  Handle | SBTBuffer | padding | Handle | SBTBuffer | padding
 *  -----------------------------------------------------------
 */
void ShaderBindingTable::AddRecord(const Shaders::SBTBuffer &data)
{
    if (m_ClosestHitGroupCount == m_ClosestHitGroupCapacity)
    {
        const uint32_t newCapacity = m_ClosestHitGroupCapacity == 0 ? 1 : m_ClosestHitGroupCapacity * 2;
        m_ClosestHitGroups.reserve(newCapacity * m_AlignedHitGroupSize);
    }

    static_assert(Utils::uploadable<Shaders::SBTBuffer>);

    // Leave space for handle
    m_ClosestHitGroups.resize(m_ClosestHitGroups.size() + m_HandleSize);

    // Copy buffer
    const auto *ptr = reinterpret_cast<const std::byte *>(&data);
    std::copy(ptr, ptr + sizeof(Shaders::SBTBuffer), std::back_inserter(m_ClosestHitGroups));

    // Skip padding
    m_ClosestHitGroupCount++;
    m_ClosestHitGroups.resize(m_ClosestHitGroupCount * m_AlignedHitGroupSize);
}

void ShaderBindingTable::Upload(vk::Pipeline pipeline)
{
    const uint32_t shaderGroupCount = 3;
    m_ShaderHandles = DeviceContext::GetLogical().getRayTracingShaderGroupHandlesKHR<std::byte>(
        pipeline, 0, shaderGroupCount, m_AlignedHandleSize * shaderGroupCount,
        Application::GetDispatchLoader()
    );

    auto closestHitHandle = std::span(m_ShaderHandles.begin() + 2 * m_AlignedHandleSize, m_HandleSize);
    for (int i = 0; i < m_ClosestHitGroupCount; i++)
        std::ranges::copy(closestHitHandle, m_ClosestHitGroups.begin() + i * m_AlignedHitGroupSize);

    BufferBuilder builder;
    builder.SetUsageFlags(
        vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eTransferSrc |
        vk::BufferUsageFlagBits::eShaderDeviceAddress
    ).SetAlignment(m_GroupBaseAlignment);

    m_RaygenTable = builder.CreateHostBuffer(
        std::span(m_ShaderHandles.begin(), m_HandleSize), "Raygen Shader Binding Table Buffer"
    );
    m_MissTable = builder.CreateHostBuffer(
        std::span(m_ShaderHandles.begin() + m_AlignedHandleSize, m_HandleSize),
        "Miss Shader Binding Table Buffer"
    );
    m_ClosestHitTable =
        builder.CreateHostBuffer(std::span(m_ClosestHitGroups), "Closest Hit Shader Binding Table Buffer");
}

vk::StridedDeviceAddressRegionKHR ShaderBindingTable::GetRaygenTableEntry() const
{
    return { m_RaygenTable.GetDeviceAddress(), m_AlignedHandleSize, m_AlignedHandleSize };
}

vk::StridedDeviceAddressRegionKHR ShaderBindingTable::GetMissTableEntry() const
{
    return { m_MissTable.GetDeviceAddress(), m_AlignedHandleSize, m_AlignedHandleSize };
}

vk::StridedDeviceAddressRegionKHR ShaderBindingTable::GetClosestHitTableEntry() const
{
    return { m_ClosestHitTable.GetDeviceAddress(), m_AlignedHitGroupSize, m_HitGroupSize };
}

}