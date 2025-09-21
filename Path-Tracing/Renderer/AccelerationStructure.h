#pragma once

#include <vulkan/vulkan.hpp>

#include <memory>
#include <span>
#include <vector>

#include "Scene.h"

#include "Buffer.h"

namespace PathTracing
{

class AccelerationStructure
{
public:
    AccelerationStructure(
        vk::DeviceAddress vertexBufferAddress, vk::DeviceAddress indexBufferAddress,
        vk::DeviceAddress animatedVertexBufferAddress, vk::DeviceAddress animatedIndexBufferAddress,
        vk::DeviceAddress transformBufferAddress, std::shared_ptr<const Scene> scene, uint32_t hitGroupCount
    );
    ~AccelerationStructure();

    AccelerationStructure(const AccelerationStructure &) = delete;
    AccelerationStructure &operator=(const AccelerationStructure &) = delete;

    void Update(vk::CommandBuffer commandBuffer);

    [[nodiscard]] vk::AccelerationStructureKHR GetTlas() const;

private:
    const vk::DeviceSize m_ScratchOffsetAlignment;

    vk::DeviceAddress m_VertexBufferAddress;
    vk::DeviceAddress m_AnimatedVertexBufferAddress;
    vk::DeviceAddress m_IndexBufferAddress;
    vk::DeviceAddress m_AnimatedIndexBufferAddress;
    vk::DeviceAddress m_TransformBufferAddress;

    const uint32_t m_HitGroupCount;
    std::shared_ptr<const Scene> m_Scene;

    Buffer m_InstanceBuffer;

    Buffer m_BlasBuffer;
    Buffer m_BlasScratchBuffer;

    struct BlasInfo
    {
        std::vector<vk::AccelerationStructureBuildRangeInfoKHR> Ranges;
        std::vector<vk::AccelerationStructureGeometryKHR> Geometries;
        vk::AccelerationStructureBuildGeometryInfoKHR BuildInfo;

        vk::DeviceSize BlasBufferOffset = 0;
        vk::DeviceSize BlasScratchBufferOffset = 0;
        vk::DeviceSize BlasBufferSize = 0;
        bool IsAnimated = false;
    };

    struct Blas
    {
        vk::AccelerationStructureKHR Handle;
        vk::DeviceAddress Address;
    };

    std::vector<BlasInfo> m_BlasInfos;
    std::vector<Blas> m_Blases;

    bool m_IsOpaque = true;
    Buffer m_TlasBuffer;
    Buffer m_TlasScratchBuffer;
    vk::AccelerationStructureKHR m_Tlas;

private:
    void CreateBlases();
    void BuildBlases(vk::CommandBuffer commandBuffer, vk::BuildAccelerationStructureModeKHR mode);

    void CreateTlas();
    void BuildTlas(vk::CommandBuffer commandBuffer, vk::BuildAccelerationStructureModeKHR mode);

    void AddBuildSyncBarrier(vk::CommandBuffer commandBuffer);
    void AddTraceBarrier(vk::CommandBuffer commandBuffer);

    [[nodiscard]] vk::BuildAccelerationStructureFlagsKHR GetFlags(bool isAnimated) const;
};

}
