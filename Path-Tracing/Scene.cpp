#include <glm/ext/matrix_relational.hpp>

#include <limits>
#include <ranges>

#include "Core/Core.h"

#include "Scene.h"

namespace PathTracing
{

void Animation::Update(float timeStep, std::span<SceneNode> nodes)
{
    CurrentTick += timeStep * TickPerSecond;
    if (CurrentTick >= TickDuration)
    {
        for (AnimationNode& node : Nodes)
        {
            node.Positions.Index = 0;
            node.Rotations.Index = 0;
            node.Scales.Index = 0;
        }
    }

    while (CurrentTick >= TickDuration)
        CurrentTick -= TickDuration;

    for (AnimationNode &node : Nodes)
    {
        glm::vec3 position = node.Positions.Update(CurrentTick);
        glm::quat rotation = node.Rotations.Update(CurrentTick);
        glm::vec3 scale = node.Scales.Update(CurrentTick);
      
        nodes[node.SceneNodeIndex].Transform =
            glm::transpose(glm::scale(glm::translate(glm::mat4(1.0f), position) * glm::mat4(rotation), scale)
            );
    }
}

uint32_t SceneGraph::AddSceneNode(SceneNode &&node)
{
    m_SceneNodes.push_back(std::move(node));
    return m_SceneNodes.size() - 1;
}

void SceneGraph::AddAnimation(Animation &&animation)
{
    return m_Animations.push_back(std::move(animation));
}

void SceneGraph::UpdateTransforms()
{
#ifndef NDEBUG
    std::vector<bool> isUpdated(m_SceneNodes.size());
#endif

    m_SceneNodes[0].CurrentTransform = m_SceneNodes[0].Transform;
    isUpdated[0] = true;

    for (int i = 1; i < m_SceneNodes.size(); i++)
    {
        SceneNode &node = m_SceneNodes[i];
        SceneNode &parent = m_SceneNodes[node.Parent];
        assert(isUpdated[node.Parent] == true);  // Nodes are not in pre-order sequence
        assert(isUpdated[i] == false);  // Two animations have the same SceneNode or it's a DAG not a tree

        node.CurrentTransform = node.Transform * parent.CurrentTransform;
#ifndef NDEBUG
        isUpdated[i] = true;
#endif
    }
}

void SceneGraph::Update(float timeStep)
{
    for (Animation &animation : m_Animations)
        animation.Update(timeStep, m_SceneNodes);

    UpdateTransforms();
}

std::span<const SceneNode> SceneGraph::GetSceneNodes() const
{
    return m_SceneNodes;
}

Scene::Scene()
{
    m_Transforms.push_back(glm::mat4(1.0f));
}

void Scene::Update(float timeStep)
{
    m_Graph.Update(timeStep);

    auto nodes = m_Graph.GetSceneNodes();

    // TODO: Resize only in SceneBuilder::Build()
    m_ModelInstances.resize(m_ModelInstanceInfos.size());

    for (int i = 0; i < m_ModelInstanceInfos.size(); i++)
    {
        m_ModelInstances[i].ModelIndex = m_ModelInstanceInfos[i].ModelIndex;  // TODO: This also only in SB::Build()
        m_ModelInstances[i].Transform = nodes[m_ModelInstanceInfos[i].SceneNodeIndex].CurrentTransform;
    }
}

uint32_t Scene::AddSceneNode(SceneNode &&node)
{
    return m_Graph.AddSceneNode(std::move(node));
}

void Scene::AddAnimation(Animation &&animation)
{
    return m_Graph.AddAnimation(std::move(animation));
}

uint32_t Scene::AddGeometry(Geometry &&geometry)
{
    logger::trace(
        "Added Geometry to Scene with {} vertices and {} indices", geometry.VertexLength, geometry.IndexLength
    );

    m_Geometries.push_back(geometry);
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

uint32_t Scene::AddModelInstance(ModelInstanceInfo &&info)
{
    m_ModelInstanceInfos.push_back(std::move(info));
    return m_ModelInstanceInfos.size() - 1;
}

uint32_t Scene::AddTexture(TextureInfo &&texture)
{
    const std::string name = texture.Path.string();

    if (m_TextureIndices.contains(name))
        return m_TextureIndices[name];

    m_Textures.push_back(std::move(texture));
    const uint32_t textureIndex = Shaders::GetSceneTextureIndex(m_Textures.size() - 1);

    m_TextureIndices[name] = textureIndex;
    logger::trace("Added texture {} to Scene", name);

    return textureIndex;
}

uint32_t Scene::AddMaterial(std::string name, Shaders::Material material)
{
    if (m_MaterialIndices.contains(name))
        return m_MaterialIndices[name];

    m_Materials.push_back(material);

    m_MaterialIndices[std::move(name)] = m_Materials.size() - 1;

    logger::trace("Added material {} to Scene", name);

    return m_Materials.size() - 1;
}

void Scene::SetVertices(std::vector<Shaders::Vertex> &&vertices)
{
    m_Vertices = std::move(vertices);
}

void Scene::SetIndices(std::vector<uint32_t> &&indices)
{
    m_Indices = std::move(indices);
}

void Scene::SetSkybox(Skybox2D &&skybox)
{
    m_Skybox = skybox;
}

void Scene::SetSkybox(SkyboxCube &&skybox)
{
    m_Skybox = skybox;
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

std::span<const TextureInfo> Scene::GetTextures() const
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

const Scene::SkyboxVariant &Scene::GetSkybox() const
{
    return m_Skybox;
}

}