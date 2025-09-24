#include <glm/ext/matrix_relational.hpp>

#include <limits>
#include <ranges>

#include "Core/Core.h"

#include "Scene.h"

namespace PathTracing
{

Scene::Scene(
    std::string name, std::vector<Shaders::Vertex> &&vertices,
    std::vector<Shaders::AnimatedVertex> &&animatedVertices, std::vector<uint32_t> &&indices,
    std::vector<uint32_t> &&animatedIndices, std::vector<glm::mat3x4> &&transforms,
    std::vector<Geometry> &&geometries, std::vector<Shaders::Material> &&materials,
    std::vector<TextureInfo> &&textures, std::vector<Model> &&models,
    std::vector<ModelInstance> &&modelInstances, std::vector<Bone> &&bones, SceneGraph &&sceneGraph,
    std::vector<LightInfo> &&lightInfos, std::vector<Shaders::Light> &&lights, SkyboxVariant &&skybox,
    const std::vector<CameraInfo> &cameraInfos
)
    : m_Name(std::move(name)), m_Vertices(std::move(vertices)),
      m_AnimatedVertices(std::move(animatedVertices)), m_Indices(std::move(indices)),
      m_AnimatedIndices(std::move(animatedIndices)), m_Transforms(std::move(transforms)),
      m_Geometries(std::move(geometries)), m_Materials(std::move(materials)), m_Textures(std::move(textures)),
      m_Models(std::move(models)), m_ModelInstances(std::move(modelInstances)), m_Bones(std::move(bones)),
      m_BoneTransforms(m_Bones.size()), m_Graph(std::move(sceneGraph)), m_LightInfos(std::move(lightInfos)),
      m_Lights(std::move(lights)), m_Skybox(std::move(skybox)), m_ActiveCameraId(g_InputCameraId)
{
    auto nodes = m_Graph.GetSceneNodes();

    m_SceneCameras.reserve(cameraInfos.size());
    for (const auto &info : cameraInfos)
        m_SceneCameras.emplace_back(
            info.VerticalFOV, info.NearClip, info.FarClip, info.Position, info.Direction, info.UpDirection,
            nodes[info.SceneNodeIndex].CurrentTransform
        );

    m_HasSkeletalAnimations = std::ranges::any_of(m_Geometries, [](const auto &g) { return g.IsAnimated; });
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

    for (int i = 0; i < m_Bones.size(); i++)
        m_BoneTransforms[i] = m_Bones[i].Offset * nodes[m_Bones[i].SceneNodeIndex].CurrentTransform;

    for (int i = 0; i < m_LightInfos.size(); i++)
        m_Lights[i].Position = glm::vec4(m_LightInfos[i].Position, 1.0f) *
                               nodes[m_LightInfos[i].SceneNodeIndex].CurrentTransform;

    GetActiveCamera().OnUpdate(timeStep);
}

uint32_t SceneBuilder::AddSceneNode(SceneNode &&node)
{
    m_SceneNodes.push_back(std::move(node));
    m_IsRelativeTransform.push_back(true);
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
    Model model = CreateModel(meshInfos);
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

void SceneBuilder::SetAnimatedVertices(std::vector<Shaders::AnimatedVertex> &&vertices)
{
    m_AnimatedVertices = vertices;
}

void SceneBuilder::SetAnimatedIndices(std::vector<uint32_t> &&indices)
{
    m_AnimatedIndices = indices;
}

uint32_t SceneBuilder::AddBone(Bone &&bone)
{
    assert(m_Bones.size() < Shaders::MaxBones);

    m_Bones.push_back(std::move(bone));
    return m_Bones.size() - 1;
}

void SceneBuilder::SetAbsoluteTransform(uint32_t sceneNodeIndex)
{
    m_IsRelativeTransform[sceneNodeIndex] = false;
}

void SceneBuilder::AddLight(Shaders::Light &&light, uint32_t sceneNodeIndex)
{
    m_LightInfos.emplace_back(sceneNodeIndex, light.Position);
    m_Lights.push_back(std::move(light));
    assert(m_LightInfos.size() <= Shaders::MaxLightCount);
}

void SceneBuilder::SetSkybox(Skybox2D &&skybox)
{
    m_Skybox = skybox;
}

void SceneBuilder::SetSkybox(SkyboxCube &&skybox)
{
    m_Skybox = skybox;
}

void SceneBuilder::AddCamera(CameraInfo &&camera)
{
    m_CameraInfos.push_back(camera);
}

std::shared_ptr<Scene> SceneBuilder::CreateSceneShared(std::string name)
{
    std::vector<ModelInstance> modelInstances;
    modelInstances.reserve(m_ModelInstanceInfos.size());
    for (const auto &info : m_ModelInstanceInfos)
        modelInstances.emplace_back(info.first, info.second, m_SceneNodes[info.second].Transform);

    if (m_Lights.empty())
        m_Lights.push_back(g_DefaultLight);

    auto scene = std::make_shared<Scene>(
        std::move(name), std::move(m_Vertices), std::move(m_AnimatedVertices), std::move(m_Indices),
        std::move(m_AnimatedIndices), std::move(m_Transforms), std::move(m_Geometries),
        std::move(m_Materials), std::move(m_Textures), std::move(m_Models), std::move(modelInstances),
        std::move(m_Bones),
        SceneGraph(std::move(m_SceneNodes), std::move(m_IsRelativeTransform), std::move(m_Animations)),
        std::move(m_LightInfos), std::move(m_Lights), std::move(m_Skybox), m_CameraInfos
    );

    m_SbtOffset = 0;
    m_Vertices.clear();
    m_AnimatedVertices.clear();
    m_Indices.clear();
    m_AnimatedIndices.clear();
    m_Transforms = { glm::mat3x4(1.0f) };
    m_Geometries.clear();
    m_Materials.clear();
    m_MaterialIndices.clear();
    m_Textures.clear();
    m_TextureIndices.clear();
    m_Models.clear();
    m_ModelInstanceInfos.clear();
    m_Bones.clear();
    m_SceneNodes.clear();
    m_Animations.clear();
    m_Lights.clear();
    m_LightInfos.clear();
    m_Skybox = SkyboxClearColor {};
    m_CameraInfos.clear();

    return scene;
}

Model SceneBuilder::CreateModel(std::span<const MeshInfo> meshInfos)
{
    Model model = { {}, m_SbtOffset };
    for (const MeshInfo &meshInfo : meshInfos)
    {
        const bool isIdentity = glm::all(glm::equal(meshInfo.Transform, glm::mat3x4(1.0f)));

        model.Meshes.emplace_back(
            meshInfo.GeometryIndex, meshInfo.MaterialIndex,
            isIdentity ? IdentityTransformIndex : static_cast<uint32_t>(m_Transforms.size())
        );

        if (!isIdentity)
            m_Transforms.push_back(meshInfo.Transform);
    }

    m_SbtOffset += meshInfos.size();
    return model;
}

std::span<const Shaders::Vertex> Scene::GetVertices() const
{
    return m_Vertices;
}

std::span<const Shaders::AnimatedVertex> Scene::GetAnimatedVertices() const
{
    return m_AnimatedVertices;
}

std::span<const uint32_t> Scene::GetIndices() const
{
    return m_Indices;
}

std::span<const uint32_t> Scene::GetAnimatedIndices() const
{
    return m_AnimatedIndices;
}

std::span<const glm::mat3x4> Scene::GetTransforms() const
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

std::span<const glm::mat3x4> Scene::GetBoneTransforms() const
{
    return m_BoneTransforms;
}

bool Scene::HasAnimations() const
{
    return m_Graph.HasAnimations();
}

bool Scene::HasSkeletalAnimations() const
{
    return m_HasSkeletalAnimations;
}

std::span<const Shaders::Light> Scene::GetLights() const
{
    return m_Lights;
}

const SkyboxVariant &Scene::GetSkybox() const
{
    return m_Skybox;
}

uint32_t Scene::GetSceneCamerasCount() const
{
    return m_SceneCameras.size();
}

CameraId Scene::GetActiveCameraId() const
{
    return m_ActiveCameraId;
}

Camera &Scene::GetActiveCamera()
{
    if (m_ActiveCameraId == g_InputCameraId)
        return m_InputCamera;
    return m_SceneCameras[m_ActiveCameraId];
}

void Scene::SetActiveCamera(CameraId id)
{
    Camera *camera = &m_InputCamera;
    if (id != g_InputCameraId)
        camera = &m_SceneCameras[id];

    auto [width, height] = GetActiveCamera().GetExtent();
    camera->OnResize(width, height);
    m_ActiveCameraId = id;
}

uint32_t Scene::GetDefaultTextureIndex(TextureType type)
{
    switch (type)
    {
    case TextureType::Color:
        return Shaders::DefaultColorTextureIndex;
    case TextureType::Normal:
        return Shaders::DefaultNormalTextureIndex;
    case TextureType::Roughness:
        return Shaders::DefaultRoughnessTextureIndex;
    case TextureType::Metalic:
        return Shaders::DefaultMetalicTextureIndex;
    default:
        throw error(std::format("Unsupported Texture type {}", static_cast<uint8_t>(type)));
    }
}

}