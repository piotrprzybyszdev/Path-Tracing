#include <glm/ext/matrix_relational.hpp>

#include <ranges>

#include "Core/Core.h"

#include "Scene.h"

namespace PathTracing
{

Scene::Scene(
    std::vector<Shaders::Vertex> &&vertices, std::vector<Shaders::AnimatedVertex> &&animatedVertices,
    std::vector<uint32_t> &&indices, std::vector<uint32_t> &&animatedIndices,
    std::vector<glm::mat3x4> &&transforms, std::vector<Geometry> &&geometries,
    std::vector<Shaders::MetalicRoughnessMaterial> &&MetalicRoughnessMaterials, std::vector<TextureInfo> &&textures,
    std::vector<Shaders::SpecularGlossinessMaterial> &&solidColorMaterials, std::vector<Model> &&models,
    std::vector<ModelInstance> &&modelInstances, std::vector<Bone> &&bones, SceneGraph &&sceneGraph,
    std::vector<LightInfo> &&lightInfos, std::vector<Shaders::PointLight> &&pointLights,
    Shaders::DirectionalLight &&directionalLight, SkyboxVariant &&skybox,
    const std::vector<CameraInfo> &cameraInfos, bool hasAnimatedInstances
)
    : m_Vertices(std::move(vertices)), m_AnimatedVertices(std::move(animatedVertices)),
      m_Indices(std::move(indices)), m_AnimatedIndices(std::move(animatedIndices)),
      m_Transforms(std::move(transforms)), m_Geometries(std::move(geometries)),
      m_MetalicRoughnessMaterials(std::move(MetalicRoughnessMaterials)), m_Textures(std::move(textures)),
      m_SpecularGlossinessMaterials(std::move(solidColorMaterials)), m_Models(std::move(models)),
      m_ModelInstances(std::move(modelInstances)), m_Bones(std::move(bones)),
      m_BoneTransforms(m_Bones.size()), m_Graph(std::move(sceneGraph)), m_LightInfos(std::move(lightInfos)),
      m_PointLights(std::move(pointLights)), m_DirectionalLight(std::move(directionalLight)),
      m_Skybox(std::move(skybox)), m_ActiveCameraId(g_InputCameraId),
      m_HasAnimatedInstances(hasAnimatedInstances)
{
    auto nodes = m_Graph.GetSceneNodes();

    m_SceneCameras.reserve(cameraInfos.size());
    for (const auto &info : cameraInfos)
        m_SceneCameras.emplace_back(
            info.VerticalFOV, info.NearClip, info.FarClip, info.Position, info.Direction, info.UpDirection,
            nodes[info.SceneNodeIndex].CurrentTransform
        );

    m_HasSkeletalAnimations =
        std::any_of(m_Geometries.begin(), m_Geometries.end(), [](const auto &g) { return g.IsAnimated; });
}

bool Scene::Update(float timeStep)
{
    m_Graph.Update(timeStep);

    auto nodes = m_Graph.GetSceneNodes();

    for (auto &instance : m_ModelInstances)
        instance.Transform = nodes[instance.SceneNodeIndex].CurrentTransform;

    for (int i = 0; i < m_Bones.size(); i++)
        m_BoneTransforms[i] = m_Bones[i].Offset * nodes[m_Bones[i].SceneNodeIndex].CurrentTransform;

    for (int i = 0; i < m_LightInfos.size(); i++)
        m_PointLights[i].Position = glm::vec4(m_LightInfos[i].Position, 1.0f) *
                                    nodes[m_LightInfos[i].SceneNodeIndex].CurrentTransform;
    
    bool updated = m_HasAnimatedInstances;
    
    updated |= m_HasCameraChanged;
    m_HasCameraChanged = false;

    updated |= GetActiveCamera().OnUpdate(timeStep);
    return updated;
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
    if (m_TextureIndices.contains(texture.Name))
        return m_TextureIndices[texture.Name];

    assert(m_Textures.size() < Shaders::MaxTextureCount);

    m_Textures.push_back(std::move(texture));
    const std::string &textureName = m_Textures.back().Name;
    const uint32_t textureIndex = Shaders::GetSceneTextureIndex(m_Textures.size() - 1);

    m_TextureIndices[textureName] = textureIndex;
    logger::trace("Added texture {} to Scene", textureName);

    return textureIndex;
}

Shaders::MaterialId SceneBuilder::AddMaterial(std::string name, Shaders::MetalicRoughnessMaterial material)
{
    if (m_MetalicRoughnessMaterialIds.contains(name))
        return m_MetalicRoughnessMaterialIds[name];

    assert(m_MetalicRoughnessMaterials.size() < Shaders::MaxMaterialCount);
    m_MetalicRoughnessMaterials.push_back(material);

    Shaders::MaterialId materialId = Shaders::CreateMaterialId(
        m_MetalicRoughnessMaterials.size() - 1, Shaders::MaterialTypeMetalicRoughness
    );
    
    m_MetalicRoughnessMaterialIds[std::move(name)] = materialId;
    logger::trace("Added textured material {} to Scene", name);

    return materialId;
}

Shaders::MaterialId SceneBuilder::AddMaterial(std::string name, Shaders::SpecularGlossinessMaterial material)
{
    if (m_SpecularGlossinessMaterialIds.contains(name))
        return m_SpecularGlossinessMaterialIds[name];

    assert(m_SpecularGlossinessMaterials.size() < Shaders::MaxMaterialCount);
    m_SpecularGlossinessMaterials.push_back(material);

    Shaders::MaterialId materialId = Shaders::CreateMaterialId(
        m_SpecularGlossinessMaterials.size() - 1, Shaders::MaterialTypeSpecularGlossiness
    );
    
    m_SpecularGlossinessMaterialIds[std::move(name)] = materialId;
    logger::trace("Added solid color material {} to Scene", name);

    return materialId;
}

std::vector<Shaders::Vertex> &SceneBuilder::GetVertices()
{
    return m_Vertices;
}

std::vector<uint32_t> &SceneBuilder::GetIndices()
{
    return m_Indices;
}

std::vector<Shaders::AnimatedVertex> &SceneBuilder::GetAnimatedVertices()
{
    return m_AnimatedVertices;
}

std::vector<uint32_t> &SceneBuilder::GetAnimatedIndices()
{
    return m_AnimatedIndices;
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

void SceneBuilder::AddLight(Shaders::PointLight &&light, uint32_t sceneNodeIndex)
{
    assert(m_LightInfos.size() < Shaders::MaxLightCount);
    m_LightInfos.emplace_back(sceneNodeIndex, light.Position);
    m_PointLights.push_back(std::move(light));
}

void SceneBuilder::SetDirectionalLight(Shaders::DirectionalLight &&light, uint32_t sceneNodeIndex)
{
    light.Direction = glm::vec4(light.Direction, 0.0f) * m_SceneNodes[sceneNodeIndex].Transform;
    m_DirectionalLight = std::move(light);
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

std::shared_ptr<Scene> SceneBuilder::CreateSceneShared()
{
    std::vector<bool> isAnimated(m_SceneNodes.size(), false);
    for (const Animation &animation: m_Animations)
        for (const AnimationNode &node : animation.Nodes)
            isAnimated[node.SceneNodeIndex] = true;

    for (int i = 0; i < m_SceneNodes.size(); i++)
        if (isAnimated[m_SceneNodes[i].Parent])
            isAnimated[i] = true;

    bool hasAnimatedInstances = !m_Bones.empty();
    for (auto [sceneNodeIndex, _] : m_LightInfos)
        hasAnimatedInstances |= isAnimated[sceneNodeIndex];

    std::vector<ModelInstance> modelInstances;
    modelInstances.reserve(m_ModelInstanceInfos.size());
    for (auto [modelIndex, sceneNodeIndex] : m_ModelInstanceInfos)
    {
        modelInstances.emplace_back(modelIndex, sceneNodeIndex, m_SceneNodes[sceneNodeIndex].Transform);
        hasAnimatedInstances |= isAnimated[sceneNodeIndex];
    }

    auto scene = std::make_shared<Scene>(
        std::move(m_Vertices), std::move(m_AnimatedVertices), std::move(m_Indices),
        std::move(m_AnimatedIndices), std::move(m_Transforms), std::move(m_Geometries),
        std::move(m_MetalicRoughnessMaterials), std::move(m_Textures), std::move(m_SpecularGlossinessMaterials),
        std::move(m_Models), std::move(modelInstances), std::move(m_Bones),
        SceneGraph(std::move(m_SceneNodes), std::move(m_IsRelativeTransform), std::move(m_Animations)),
        std::move(m_LightInfos), std::move(m_PointLights), std::move(m_DirectionalLight), std::move(m_Skybox),
        std::move(m_CameraInfos), hasAnimatedInstances
    );

    m_MeshOffset = 0;
    m_Vertices.clear();
    m_AnimatedVertices.clear();
    m_Indices.clear();
    m_AnimatedIndices.clear();
    m_Transforms = { glm::mat3x4(1.0f) };
    m_Geometries.clear();
    m_MetalicRoughnessMaterials.clear();
    m_MetalicRoughnessMaterialIds.clear();
    m_SpecularGlossinessMaterials.clear();
    m_SpecularGlossinessMaterialIds.clear();
    m_Textures.clear();
    m_TextureIndices.clear();
    m_Models.clear();
    m_ModelInstanceInfos.clear();
    m_Bones.clear();
    m_SceneNodes.clear();
    m_SceneNodes.push_back(SceneNode { RootNodeIndex, glm::mat4(1.0f), glm::mat4(1.0f) });
    m_IsRelativeTransform.clear();
    m_IsRelativeTransform.push_back(true);
    m_Animations.clear();
    m_LightInfos.clear();
    m_PointLights.clear();
    m_DirectionalLight = g_DefaultLight;
    m_Skybox = SkyboxClearColor {};
    m_CameraInfos.clear();

    return scene;
}

Model SceneBuilder::CreateModel(std::span<const MeshInfo> meshInfos)
{
    Model model = { {}, m_MeshOffset };
    for (const MeshInfo &meshInfo : meshInfos)
    {
        const bool isIdentity = glm::all(glm::equal(meshInfo.Transform, glm::mat3x4(1.0f)));

        model.Meshes.emplace_back(
            meshInfo.GeometryIndex, meshInfo.MaterialIndex, meshInfo.ShaderMaterialType,
            isIdentity ? IdentityTransformIndex : static_cast<uint32_t>(m_Transforms.size())
        );

        if (!isIdentity)
            m_Transforms.push_back(meshInfo.Transform);
    }

    m_MeshOffset += meshInfos.size();
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

std::span<const Shaders::MetalicRoughnessMaterial> Scene::GetMetalicRoughnessMaterials() const
{
    return m_MetalicRoughnessMaterials;
}

std::span<const Shaders::SpecularGlossinessMaterial> Scene::GetSpecularGlossinessMaterials() const
{
    return m_SpecularGlossinessMaterials;
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

std::span<const Shaders::PointLight> Scene::GetPointLights() const
{
    return m_PointLights;
}

const Shaders::DirectionalLight &Scene::GetDirectionalLight() const
{
    return m_DirectionalLight;
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
    if (m_ActiveCameraId == id)
        return;

    Camera *camera = &m_InputCamera;
    if (id != g_InputCameraId)
        camera = &m_SceneCameras[id];

    auto [width, height] = GetActiveCamera().GetExtent();
    camera->OnResize(width, height);
    m_ActiveCameraId = id;
    m_HasCameraChanged = true;
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
    case TextureType::Emisive:
        return Shaders::DefaultEmissiveTextureIndex;
    case TextureType::Specular:
        return Shaders::DefaultColorTextureIndex;
    default:
        throw error(std::format("Unsupported Texture type {}", static_cast<uint8_t>(type)));
    }
}

}
