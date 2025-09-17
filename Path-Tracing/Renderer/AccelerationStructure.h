#pragma once

#include <vulkan/vulkan.hpp>

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
        const Scene &scene
    );
    ~AccelerationStructure();

    AccelerationStructure(const AccelerationStructure &) = delete;
    AccelerationStructure &operator=(const AccelerationStructure &) = delete;

    void Build();

    [[nodiscard]] vk::AccelerationStructureKHR GetTlas() const;

private:
    const vk::DeviceSize m_ScratchOffsetAlignment;

    const Buffer &m_VertexBuffer;
    const Buffer &m_IndexBuffer;
    const Buffer &m_TransformBuffer;

    const Scene &m_Scene;

    bool m_FirstBuild = true;

    Buffer m_InstanceBuffer;

    Buffer m_BlasBuffer;
    Buffer m_BlasScratchBuffer;
    std::vector<vk::AccelerationStructureKHR> m_Blases;

    Buffer m_TlasBuffer;
    Buffer m_TlasScratchBuffer;
    vk::AccelerationStructureKHR m_Tlas;

private:
    void BuildBlases();
    void BuildTlas();
};

}
