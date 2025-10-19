#pragma once

#include <vulkan/vulkan.hpp>

#include <span>
#include <vector>

#include "Shaders/ShaderRendererTypes.incl"

#include "Buffer.h"

namespace PathTracing
{

struct SBTEntryInfo
{
    uint32_t HitGroupIndex;
    Shaders::SBTBuffer Buffer;
};

class ShaderBindingTable
{
public:
    ShaderBindingTable(uint32_t hitGroupCount);
    ~ShaderBindingTable();

    ShaderBindingTable(const ShaderBindingTable &) = delete;
    ShaderBindingTable &operator=(const ShaderBindingTable) = delete;

    void AddRecord(std::span<const SBTEntryInfo> entries);

    void Upload(vk::Pipeline pipeline, uint32_t raygenIndex, std::span<const uint32_t> missGroupIndices);

    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetRaygenTableEntry() const;
    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetMissTableEntry() const;
    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetClosestHitTableEntry() const;

private:
    const uint32_t m_HitGroupCount;
    const uint32_t m_HandleSize;
    const uint32_t m_HitGroupSize;
    const uint32_t m_AlignedHandleSize;
    const uint32_t m_AlignedHitGroupSize;
    const uint32_t m_GroupBaseAlignment;
    const uint32_t m_HitRecordSize;
    const uint32_t m_RaygenRecordSize;
    const uint32_t m_RaygenTableSize;
    const uint32_t m_MissRecordSize;
    const uint32_t m_MissTableSize;

    uint32_t m_MaxShaderGroupIndex = 0;
    std::vector<std::byte> m_ShaderHandles;

    uint32_t m_ClosestHitRecordCount = 0;
    uint32_t m_ClosestHitRecordCapacity = 0;
    std::vector<std::byte> m_HitGroupRecords;
    std::vector<uint32_t> m_HitGroupIndices;

    Buffer m_TableBuffer;
    vk::DeviceAddress m_TableBufferDeviceAddress = 0;
};

}