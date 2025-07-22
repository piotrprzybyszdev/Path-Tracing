#pragma once

#include <optional>
#include <span>
#include <vector>

#include <vulkan/vulkan.hpp>

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

    void Build();

    [[nodiscard]] vk::AccelerationStructureKHR GetTlas() const;

private:
    const vk::DeviceSize m_ScratchOffsetAlignment;

    const Buffer &m_VertexBuffer;
    const Buffer &m_IndexBuffer;
    const Buffer &m_TransformBuffer;

    const Scene &m_Scene;

    std::unique_ptr<Buffer> m_BlasBuffer;
    std::vector<vk::AccelerationStructureKHR> m_Blases;

    std::unique_ptr<Buffer> m_TlasBuffer;
    vk::AccelerationStructureKHR m_Tlas;

private:
    void BuildBlases();
    void BuildTlas();
};

}
