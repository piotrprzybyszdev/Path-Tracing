#pragma once

#include <glm/glm.hpp>

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Core/Camera.h"

#include "Shaders/ShaderTypes.incl"

#include "SceneGraph.h"

namespace PathTracing
{

enum class TextureType : uint8_t
{
    Emisive,
    Color,
    Normal,
    Roughness,
    Metallic,
    Specular,
    Glossiness,
    Shininess,
    Skybox,
};

enum class TextureFormat : uint8_t
{
    RGBAU8,
    RGBAF32,
    BC1,
    BC3,
    BC5,
};

using FileTextureSource = std::filesystem::path;
using MemoryTextureSource = std::span<const uint8_t>;
using TextureSourceVariant = std::variant<FileTextureSource, MemoryTextureSource>;

struct TextureInfo
{
    using LoaderType = uint16_t;

    TextureType Type;
    TextureFormat Format;
    LoaderType Loader;
    uint32_t Levels;
    uint32_t Width, Height;
    std::string Name;
    TextureSourceVariant Source;
};

using TextureData = std::span<std::byte>;

struct Geometry
{
    uint32_t VertexOffset;
    uint32_t VertexLength;
    uint32_t IndexOffset;
    uint32_t IndexLength;
    bool IsOpaque;
    bool IsAnimated;
};

enum class MaterialType : uint8_t
{
    MetallicRoughness,
    SpecularGlossiness,
    Phong,
};

struct MeshInfo
{
    uint32_t GeometryIndex;
    uint32_t MaterialIndex;
    MaterialType ShaderMaterialType;
    glm::mat3x4 Transform;
};

struct Mesh
{
    uint32_t GeometryIndex;
    uint32_t MaterialIndex;
    MaterialType ShaderMaterialType;
    uint32_t TransformBufferOffset;
};

struct Model
{
    std::vector<Mesh> Meshes;
    uint32_t MeshOffset;
};

struct ModelInstance
{
    uint32_t ModelIndex;
    uint32_t SceneNodeIndex;
    glm::mat4 Transform;
};

struct Bone
{
    uint32_t SceneNodeIndex;
    glm::mat4 Offset;
};

struct LightInfo
{
    uint32_t SceneNodeIndex;
    glm::vec3 Position;
};

struct DirectionalLightInfo
{
    uint32_t SceneNodeIndex;
    glm::vec3 Direction;
};

struct SkyboxClearColor
{
};

struct Skybox2D
{
    TextureInfo Content;
};

struct SkyboxCube
{
    TextureInfo Front;
    TextureInfo Back;
    TextureInfo Up;
    TextureInfo Down;
    TextureInfo Left;
    TextureInfo Right;
};

struct CameraInfo
{
    float VerticalFOV;
    float NearClip;
    float FarClip;
    glm::vec3 Position;
    glm::vec3 Direction;
    glm::vec3 UpDirection;
    uint32_t SceneNodeIndex;
};

using SkyboxVariant = std::variant<SkyboxClearColor, Skybox2D, SkyboxCube>;
using CameraId = int32_t;

class Scene
{
public:
    Scene(
        std::vector<Shaders::Vertex> &&vertices, std::vector<Shaders::AnimatedVertex> &&animatedVertices,
        std::vector<uint32_t> &&indices, std::vector<uint32_t> &&animatedIndices,
        std::vector<glm::mat3x4> &&transforms, std::vector<Geometry> &&geometries,
        std::vector<Shaders::MetallicRoughnessMaterial> &&metallicRoughnessMaterials,
        std::vector<TextureInfo> &&textures,
        std::vector<Shaders::SpecularGlossinessMaterial> &&specularGlossinessMaterials,
        std::vector<Shaders::PhongMaterial> &&phongMaterials, std::vector<Model> &&models,
        std::vector<ModelInstance> &&modelInstances, std::vector<Bone> &&bones, SceneGraph &&sceneGraph,
        std::vector<LightInfo> &&lightInfos, DirectionalLightInfo &&directionalLightInfo,
        std::vector<Shaders::PointLight> &&pointLights, Shaders::DirectionalLight &&directionalLight,
        SkyboxVariant &&skybox, const std::vector<CameraInfo> &cameraInfos, bool hasAnimatedInstances,
        bool hasDxNormalTextures, bool forceFullTextureSize, const std::string &name
    );

    bool Update(float timeStep);

    [[nodiscard]] const std::string &GetName() const;

    [[nodiscard]] std::span<const Shaders::Vertex> GetVertices() const;
    [[nodiscard]] std::span<const Shaders::AnimatedVertex> GetAnimatedVertices() const;
    [[nodiscard]] std::span<const uint32_t> GetIndices() const;
    [[nodiscard]] std::span<const uint32_t> GetAnimatedIndices() const;
    [[nodiscard]] std::span<const glm::mat3x4> GetTransforms() const;
    [[nodiscard]] std::span<const Geometry> GetGeometries() const;
    [[nodiscard]] std::span<const Shaders::MetallicRoughnessMaterial> GetMetallicRoughnessMaterials() const;
    [[nodiscard]] std::span<const Shaders::SpecularGlossinessMaterial> GetSpecularGlossinessMaterials() const;
    [[nodiscard]] std::span<const Shaders::PhongMaterial> GetPhongMaterials() const;
    [[nodiscard]] std::span<const TextureInfo> GetTextures() const;

    [[nodiscard]] std::span<const Model> GetModels() const;
    [[nodiscard]] std::span<const ModelInstance> GetModelInstances() const;

    [[nodiscard]] std::span<const glm::mat3x4> GetBoneTransforms() const;

    [[nodiscard]] bool HasAnimations() const;
    [[nodiscard]] bool HasSkeletalAnimations() const;
    [[nodiscard]] bool IsAnimationPaused() const;
    [[nodiscard]] bool HasDxNormalTextures() const;
    [[nodiscard]] bool GetForceFullTextureSize() const;

    [[nodiscard]] std::span<const Shaders::PointLight> GetPointLights() const;
    [[nodiscard]] const Shaders::DirectionalLight &GetDirectionalLight() const;

    [[nodiscard]] const SkyboxVariant &GetSkybox() const;

    [[nodiscard]] uint32_t GetSceneCamerasCount() const;
    [[nodiscard]] CameraId GetActiveCameraId() const;
    [[nodiscard]] Camera &GetActiveCamera();
    void SetActiveCamera(CameraId id);

    void ToggleAnimationPause();

    inline static const CameraId g_InputCameraId = -1;

public:
    [[nodiscard]] static uint32_t GetDefaultTextureIndex(TextureType type);

private:
    const std::string m_Name;

    std::vector<Shaders::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<glm::mat3x4> m_Transforms;

    std::vector<Shaders::AnimatedVertex> m_AnimatedVertices;
    std::vector<uint32_t> m_AnimatedIndices;

    std::vector<Geometry> m_Geometries;

    std::vector<Shaders::MetallicRoughnessMaterial> m_MetallicRoughnessMaterials;
    std::vector<Shaders::SpecularGlossinessMaterial> m_SpecularGlossinessMaterials;
    std::vector<Shaders::PhongMaterial> m_PhongMaterials;

    std::vector<TextureInfo> m_Textures;

    const bool m_HasDxNormalTextures = false;
    const bool m_ForceFullTextureSize = false;

    std::vector<Model> m_Models;
    std::vector<ModelInstance> m_ModelInstances;

    std::vector<Bone> m_Bones;
    std::vector<glm::mat3x4> m_BoneTransforms;

    SceneGraph m_Graph;
    bool m_HasSkeletalAnimations = false;
    bool m_HasAnimatedInstances;

    std::vector<LightInfo> m_LightInfos;
    std::vector<Shaders::PointLight> m_PointLights;
    DirectionalLightInfo m_DirectionalLightInfo;
    Shaders::DirectionalLight m_DirectionalLight;

    SkyboxVariant m_Skybox = SkyboxClearColor {};

    InputCamera m_InputCamera =
        InputCamera(45.0f, 100.0f, 0.1f, glm::vec3(3.0f, 1.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
    std::vector<AnimatedCamera> m_SceneCameras;
    CameraId m_ActiveCameraId = g_InputCameraId;
    bool m_HasCameraChanged = true;

    bool m_IsAnimationPaused = false;
};

class SceneBuilder
{
public:
    /* SceneNodes have to be added in pre-order sequence */
    uint32_t AddSceneNode(SceneNode &&node);
    void AddAnimation(Animation &&animation);

    uint32_t AddGeometry(Geometry &&geometry);
    uint32_t AddModel(std::span<const MeshInfo> meshInfos);
    uint32_t AddModelInstance(uint32_t modelIndex, uint32_t sceneNodeIndex);

    uint32_t AddTexture(TextureInfo &&texture);
    Shaders::MaterialId AddMaterial(std::string name, Shaders::MetallicRoughnessMaterial material);
    Shaders::MaterialId AddMaterial(std::string name, Shaders::SpecularGlossinessMaterial material);
    Shaders::MaterialId AddMaterial(std::string name, Shaders::PhongMaterial material);

    std::vector<Shaders::Vertex> &GetVertices();
    std::vector<uint32_t> &GetIndices();

    std::vector<Shaders::AnimatedVertex> &GetAnimatedVertices();
    std::vector<uint32_t> &GetAnimatedIndices();

    uint32_t AddBone(Bone &&bone);
    void SetAbsoluteTransform(uint32_t sceneNodeIndex);

    void AddLight(Shaders::PointLight &&light, uint32_t sceneNodeIndex);
    void SetDirectionalLight(Shaders::DirectionalLight &&light, uint32_t sceneNodeIndex);

    void SetSkybox(Skybox2D &&skybox);
    void SetSkybox(SkyboxCube &&skybox);

    void AddCamera(CameraInfo &&camera);

    void SetDxNormalTextures();
    void ForceFullTextureSize();
    [[nodiscard]] std::shared_ptr<Scene> CreateSceneShared(const std::string &name);

public:
    static inline constexpr uint32_t IdentityTransformIndex = 0;
    static inline constexpr uint32_t RootNodeIndex = 0;

private:
    std::vector<Shaders::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<glm::mat3x4> m_Transforms = { glm::mat3x4(1.0f) };

    std::vector<Shaders::AnimatedVertex> m_AnimatedVertices;
    std::vector<uint32_t> m_AnimatedIndices;

    std::vector<Geometry> m_Geometries;

    std::vector<Shaders::MetallicRoughnessMaterial> m_MetallicRoughnessMaterials;
    std::unordered_map<std::string, uint32_t> m_MetallicRoughnessMaterialIds;

    std::vector<Shaders::SpecularGlossinessMaterial> m_SpecularGlossinessMaterials;
    std::unordered_map<std::string, uint32_t> m_SpecularGlossinessMaterialIds;

    std::vector<Shaders::PhongMaterial> m_PhongMaterials;
    std::unordered_map<std::string, uint32_t> m_PhongMaterialIds;

    std::vector<TextureInfo> m_Textures;
    std::unordered_map<std::string, uint32_t> m_TextureIndices;
    bool m_HasDxNormalTextures = false;
    bool m_ForceFullTextureSize = false;

    std::vector<Model> m_Models;
    std::vector<std::pair<uint32_t, uint32_t>> m_ModelInstanceInfos;

    std::vector<SceneNode> m_SceneNodes = { SceneNode { RootNodeIndex, glm::mat4(1.0f), glm::mat4(1.0f) } };
    std::vector<bool> m_IsRelativeTransform = { true };
    std::vector<Animation> m_Animations;

    std::vector<Bone> m_Bones;

    std::vector<LightInfo> m_LightInfos;
    std::vector<Shaders::PointLight> m_PointLights;
    DirectionalLightInfo m_DirectionalLightInfo = { RootNodeIndex, g_DefaultLight.Direction };
    Shaders::DirectionalLight m_DirectionalLight = g_DefaultLight;

    SkyboxVariant m_Skybox = SkyboxClearColor {};

    std::vector<CameraInfo> m_CameraInfos;

private:
    static inline const Shaders::DirectionalLight g_DefaultLight = {
        .Color = glm::vec3(10.0f),
        .Direction = glm::vec3(-0.4f, -1.0f, -0.2f),
    };

private:
    uint32_t m_MeshOffset = 0;

    Model CreateModel(std::span<const MeshInfo> meshInfos);
};

}
