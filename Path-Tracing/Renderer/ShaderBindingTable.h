#pragma once

#include <vulkan/vulkan.hpp>

#include <span>
#include <vector>

#include "Shaders/ShaderRendererTypes.incl"

#include "Buffer.h"

namespace PathTracing
{

class ShaderBindingTable
{
public:
    ShaderBindingTable(
        uint32_t raygenGroupIndex, std::vector<uint32_t> &&missGroupIndices,
        std::vector<uint32_t> &&hitGroupIndices
    );
    ~ShaderBindingTable();

    ShaderBindingTable(const ShaderBindingTable &) = delete;
    ShaderBindingTable &operator=(const ShaderBindingTable) = delete;

    void AddRecord(std::span<const Shaders::SBTBuffer> buffers);

    void Upload(vk::Pipeline pipeline);

    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetRaygenTableEntry() const;
    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetMissTableEntry() const;
    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetClosestHitTableEntry() const;

private:
    const uint32_t m_RaygenGroupIndex;
    const std::vector<uint32_t> m_MissGroupIndices;
    const std::vector<uint32_t> m_HitGroupIndices;

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

    std::vector<std::byte> m_ShaderHandles;

    uint32_t m_ClosestHitRecordCount = 0;
    uint32_t m_ClosestHitRecordCapacity = 0;
    std::vector<std::byte> m_HitGroupRecords;

    Buffer m_TableBuffer;
    vk::DeviceAddress m_TableBufferDeviceAddress = 0;
};

}