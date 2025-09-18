#include <glm/ext/matrix_relational.hpp>

#include <limits>
#include <ranges>

#include "Core/Core.h"

#include "Scene.h"

namespace PathTracing
{

Scene::Scene(
    std::string name, std::vector<Shaders::Vertex> &&vertices, std::vector<uint32_t> &&indices,
    std::vector<glm::mat4> &&transforms, std::vector<Geometry> &&geometries,
    std::vector<Shaders::Material> &&materials, std::vector<TextureInfo> &&textures,
    std::vector<Model> &&models, std::vector<ModelInstance> &&modelInstances, SceneGraph &&sceneGraph,
    SkyboxVariant &&skybox
)
    : m_Name(std::move(name)), m_Vertices(std::move(vertices)), m_Indices(std::move(indices)),
      m_Transforms(std::move(transforms)), m_Geometries(std::move(geometries)),
      m_Materials(std::move(materials)), m_Textures(std::move(textures)), m_Models(std::move(models)),
      m_ModelInstances(std::move(modelInstances)), m_Graph(std::move(sceneGraph)), m_Skybox(std::move(skybox))
{
}

const std::string &Scene::GetName() const
{
    return m_Name;
}

void Scene::Update(float timeStep)
{
    m_Graph.Update(timeStep);

    auto nodes = m_Graph.GetSceneNodes();

    for (auto &instance : m_ModelInstances)
        instance.Transform = nodes[instance.SceneNodeIndex].CurrentTransform;
}

uint32_t SceneBuilder::AddSceneNode(SceneNode &&node)
{
    m_SceneNodes.push_back(std::move(node));
    return m_SceneNodes.size() - 1;
}

void SceneBuilder::AddAnimation(Animation &&animation)
{
    m_Animations.push_back(std::move(animation));
}

uint32_t SceneBuilder::AddGeometry(Geometry &&geometry)
{
    logger::trace(
        "Added Geometry to Scene with {} vertices and {} indices", geometry.VertexLength, geometry.IndexLength
    );

    m_Geometries.push_back(geometry);
    return m_Geometries.size() - 1;
}

uint32_t SceneBuilder::AddModel(std::span<const MeshInfo> meshInfos)
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

uint32_t SceneBuilder::AddModelInstance(uint32_t modelIndex, uint32_t sceneNodeIndex)
{
    m_ModelInstanceInfos.emplace_back(modelIndex, sceneNodeIndex);
    return m_ModelInstanceInfos.size() - 1;
}

uint32_t SceneBuilder::AddTexture(TextureInfo &&texture)
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

uint32_t SceneBuilder::AddMaterial(std::string name, Shaders::Material material)
{
    if (m_MaterialIndices.contains(name))
        return m_MaterialIndices[name];

    m_Materials.push_back(material);

    m_MaterialIndices[std::move(name)] = m_Materials.size() - 1;

    logger::trace("Added material {} to Scene", name);

    return m_Materials.size() - 1;
}

void SceneBuilder::SetVertices(std::vector<Shaders::Vertex> &&vertices)
{
    m_Vertices = std::move(vertices);
}

void SceneBuilder::SetIndices(std::vector<uint32_t> &&indices)
{
    m_Indices = std::move(indices);
}

void SceneBuilder::SetSkybox(Skybox2D &&skybox)
{
    m_Skybox = skybox;
}

void SceneBuilder::SetSkybox(SkyboxCube &&skybox)
{
    m_Skybox = skybox;
}

std::shared_ptr<Scene> SceneBuilder::CreateSceneShared(std::string name)
{
    m_ModelInstances.reserve(m_ModelInstanceInfos.size());
    for (const auto &info : m_ModelInstanceInfos)
        m_ModelInstances.emplace_back(info.first, info.second, m_SceneNodes[info.second].Transform);

    auto scene = std::make_shared<Scene>(
        std::move(name), std::move(m_Vertices), std::move(m_Indices), std::move(m_Transforms),
        std::move(m_Geometries), std::move(m_Materials), std::move(m_Textures), std::move(m_Models),
        std::move(m_ModelInstances), SceneGraph(std::move(m_SceneNodes), std::move(m_Animations)),
        std::move(m_Skybox)
    );

    m_Vertices.clear();
    m_Indices.clear();
    m_Transforms = { glm::mat4(1.0f) };
    m_Geometries.clear();
    m_Materials.clear();
    m_MaterialIndices.clear();
    m_Textures.clear();
    m_TextureIndices.clear();
    m_Models.clear();
    m_ModelInstances.clear();
    m_ModelInstanceInfos.clear();
    m_SceneNodes.clear();
    m_Animations.clear();
    m_Skybox = SkyboxClearColor {};

    return scene;
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

bool Scene::HasAnimations() const
{
    return m_Graph.HasAnimations();
}

const SkyboxVariant &Scene::GetSkybox() const
{
    return m_Skybox;
}

}