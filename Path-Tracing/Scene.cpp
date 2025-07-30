#include <limits>
#include <ranges>

#include <glm/ext/matrix_relational.hpp>

#include "Core/Core.h"

#include "Scene.h"

namespace PathTracing
{

Scene::Scene()
{
    m_Transforms.push_back(glm::mat4(1.0f));
}

uint32_t Scene::AddGeometry(
    std::span<const Shaders::Vertex> vertices, std::span<const uint32_t> indices, bool IsOpaque
)
{
    m_Geometries.emplace_back(
        static_cast<uint32_t>(m_Vertices.size()), static_cast<uint32_t>(vertices.size()),
        static_cast<uint32_t>(m_Indices.size()), static_cast<uint32_t>(indices.size()), IsOpaque
    );

    std::ranges::copy(vertices, std::back_inserter(m_Vertices));
    std::ranges::copy(indices, std::back_inserter(m_Indices));

    logger::trace("Added Geometry to Scene with {} vertices and {} indices", vertices.size(), indices.size());

    return m_Geometries.size() - 1;
}

uint32_t Scene::AddModel(std::span<const MeshInfo> meshInfos)
{
    const uint32_t sbtOffset =
        m_Models.empty() ? 0
                         : m_Models.back().SbtOffset + static_cast<uint32_t>(m_Models.back().Meshes.size());

    Model model = { {}, sbtOffset };
    for (const MeshInfo &meshInfo : meshInfos)
    {
        const bool isIdentity = glm::all(glm::equal(meshInfo.Transform, glm::mat4(1.0f)));

        model.Meshes.emplace_back(
            meshInfo.GeometryIndex, meshInfo.MaterialIndex,
            isIdentity ? IdentityTransformIndex : static_cast<uint32_t>(m_Transforms.size())
        );

        if (!isIdentity)
            m_Transforms.push_back(meshInfo.Transform);
    }

    m_Models.push_back(std::move(model));

    return m_Models.size() - 1;
}

uint32_t Scene::AddModelInstance(uint32_t modelIndex, glm::mat4 transform)
{
    m_ModelInstances.emplace_back(modelIndex, transform);
    return m_ModelInstances.size() - 1;
}

uint32_t Scene::AddMaterial(std::string name, Material material)
{
    if (m_MaterialIndices.contains(name))
        return m_MaterialIndices[name];

    m_Materials.emplace_back(
        material.Color.has_value() ? AddTexture(std::move(material.Color.value()))
                                    : Shaders::DefaultColorTextureIndex,
        material.Normal.has_value() ? AddTexture(std::move(material.Normal.value()))
                                    : Shaders::DefaultNormalTextureIndex,
        material.Roughness.has_value() ? AddTexture(std::move(material.Roughness.value()))
                                       : Shaders::DefaultRoughnessTextureIndex,
        material.Metalic.has_value() ? AddTexture(std::move(material.Metalic.value()))
                                     : Shaders::DefaultMetalicTextureIndex
    );

    m_MaterialIndices[std::move(name)] = m_Materials.size() - 1;

    logger::trace("Added material {} to Scene", name);

    return m_Materials.size() - 1;
}

uint32_t Scene::AddTexture(Texture texture)
{
    const std::string name = texture.Path.string();

    if (m_TextureIndices.contains(name))
        return m_TextureIndices[name];

    m_Textures.emplace_back(std::move(texture.Path));
    const uint32_t textureIndex = Shaders::SceneTextureOffset + m_Textures.size() - 1;

    m_TextureIndices[name] = textureIndex;
    logger::trace("Added texture {} to Scene", name);

    return textureIndex;
}

std::span<const Shaders::Vertex> Scene::GetVertices() const
{
    return m_Vertices;
}

std::span<const uint32_t> Scene::GetIndices() const
{
    return m_Indices;
}

std::span<const glm::mat4> Scene::GetTransforms() const
{
    return m_Transforms;
}

std::span<const Geometry> Scene::GetGeometries() const
{
    return m_Geometries;
}

std::span<const Shaders::Material> Scene::GetMaterials() const
{
    return m_Materials;
}

std::span<const Texture> Scene::GetTextures() const
{
    return m_Textures;
}

std::span<const Model> Scene::GetModels() const
{
    return m_Models;
}

std::span<const ModelInstance> Scene::GetModelInstances() const
{
    return m_ModelInstances;
}

}