#pragma once

#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "Buffer.h"

#include "Shaders/ShaderTypes.incl"

namespace PathTracing
{

/*
 * Geometry - set of triangles (vertices and indices)
 * Model - set of Geometries each with a transform
 * ModelInstance - a Model with a transform and SBT offset
 */

class AccelerationStructure
{
public:
    AccelerationStructure();
    ~AccelerationStructure();

    uint32_t AddGeometry(std::span<const Shaders::Vertex> vertices, std::span<const uint32_t> indices);
    uint32_t AddModel(
        const std::vector<uint32_t> &geometryIndices, std::optional<std::string_view> name = std::nullopt
    );
    uint32_t AddModel(
        const std::vector<uint32_t> &geometryIndices, const std::vector<glm::mat4> &transforms,
        std::optional<std::string_view> name = std::nullopt
    );
    void AddModelInstance(uint32_t modelIndex, glm::mat4 transform, uint32_t sbtOffset);

    void Build();

    vk::AccelerationStructureKHR GetTlas() const;

    const Buffer &GetGeometryBuffer() const;

private:
    std::vector<Shaders::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<vk::TransformMatrixKHR> m_Transforms;

    struct GeometryInfo
    {
        uint32_t VertexBufferOffset;
        uint32_t VertexBufferLength;
        uint32_t IndexBufferOffset;
        uint32_t IndexBufferLength;
    };

    std::vector<GeometryInfo> m_Geometries;

    struct ModelInfo
    {
        std::vector<uint32_t> GeometryIndices;
        std::optional<uint32_t> TransformBufferOffset;
        std::optional<std::string_view> Name;
    };

    std::vector<ModelInfo> m_Models;

    struct ModelInstanceInfo
    {
        uint32_t ModelIndex;
        glm::mat4 Transform;
        uint32_t SbtOffset;
    };

    std::vector<ModelInstanceInfo> m_ModelInstances;

    std::unique_ptr<Buffer> m_VertexBuffer = nullptr;
    std::unique_ptr<Buffer> m_IndexBuffer = nullptr;
    std::unique_ptr<Buffer> m_TransformBuffer = nullptr;
    std::unique_ptr<Buffer> m_GeometryBuffer = nullptr;

    std::unique_ptr<Buffer> m_BlasBuffer;
    std::vector<vk::AccelerationStructureKHR> m_Blases;

    std::unique_ptr<Buffer> m_TlasBuffer;
    vk::AccelerationStructureKHR m_Tlas;

private:
    vk::TransformMatrixKHR ToTransformMatrix(const glm::mat3x4 &matrix);

    void BuildBlases();
    void BuildTlas();
};

}
