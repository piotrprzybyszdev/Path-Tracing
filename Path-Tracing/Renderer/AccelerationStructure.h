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
        const Buffer &vertexBuffer, const Buffer &indexBuffer, const Buffer &transformBuffer,
        std::shared_ptr<const Scene> scene
    );
    ~AccelerationStructure();

    AccelerationStructure(const AccelerationStructure &) = delete;
    AccelerationStructure &operator=(const AccelerationStructure &) = delete;

    void Update();

    [[nodiscard]] vk::AccelerationStructureKHR GetTlas() const;

private:
    const vk::DeviceSize m_ScratchOffsetAlignment;

    const Buffer &m_VertexBuffer;
    const Buffer &m_IndexBuffer;
    const Buffer &m_TransformBuffer;

    std::shared_ptr<const Scene> m_Scene;

    Buffer m_InstanceBuffer;

    Buffer m_BlasBuffer;
    Buffer m_BlasScratchBuffer;
    
    struct Blas
    {
        vk::AccelerationStructureKHR Handle;
        vk::DeviceAddress Address;
    };

    std::vector<Blas> m_Blases;

    bool m_IsOpaque = true;
    Buffer m_TlasBuffer;
    Buffer m_TlasScratchBuffer;
    vk::AccelerationStructureKHR m_Tlas;

private:
    void CreateTlas();

    void BuildBlases();
    void BuildTlas(
        vk::BuildAccelerationStructureModeKHR mode = vk::BuildAccelerationStructureModeKHR::eUpdate
    );

    [[nodiscard]] vk::BuildAccelerationStructureFlagsKHR GetFlags() const;
};

}
