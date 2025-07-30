#pragma once

#include <vulkan/vulkan.hpp>

#include <span>
#include <vector>

#include "Shaders/ShaderRendererTypes.incl"

#include "Buffer.h"

namespace PathTracing
{

// TODO: Add support for ray types
class ShaderBindingTable
{
public:
    ShaderBindingTable();
    ~ShaderBindingTable();

    ShaderBindingTable(const ShaderBindingTable &) = delete;
    ShaderBindingTable &operator=(const ShaderBindingTable) = delete;

    void AddRecord(const Shaders::SBTBuffer &data);

    void Upload(vk::Pipeline pipeline);

    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetRaygenTableEntry() const;
    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetMissTableEntry() const;
    [[nodiscard]] vk::StridedDeviceAddressRegionKHR GetClosestHitTableEntry() const;

    static inline uint32_t RaygenGroupIndex = 0;
    static inline uint32_t MissGroupIndex = 1;
    static inline uint32_t HitGroupIndex = 2;

private:
    const uint32_t m_HandleSize;
    const uint32_t m_HitGroupSize;
    const uint32_t m_AlignedHandleSize;
    const uint32_t m_AlignedHitGroupSize;
    const uint32_t m_GroupBaseAlignment;

    std::vector<std::byte> m_ShaderHandles;

    uint32_t m_ClosestHitGroupCount = 0;
    uint32_t m_ClosestHitGroupCapacity = 0;
    std::vector<std::byte> m_ClosestHitGroups;

    Buffer m_RaygenTable;
    Buffer m_MissTable;
    Buffer m_ClosestHitTable;
};

}