#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
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
    Color,
    Normal,
    Roughness,
    Metalic,
    Skybox
};

struct TextureInfo
{
    TextureType Type;
    uint8_t Channels;
    uint32_t Width, Height;
    std::filesystem::path Path;
};

struct Geometry
{
    uint32_t VertexOffset;
    uint32_t VertexLength;
    uint32_t IndexOffset;
    uint32_t IndexLength;
    bool IsOpaque;
    bool IsAnimated;
};

struct MeshInfo
{
    uint32_t GeometryIndex;
    uint32_t MaterialIndex;
    glm::mat3x4 Transform;
};

struct Mesh
{
    uint32_t GeometryIndex;
    uint32_t MaterialIndex;
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
        std::string name, std::vector<Shaders::Vertex> &&vertices,
        std::vector<Shaders::AnimatedVertex> &&animatedVertices, std::vector<uint32_t> &&indices,
        std::vector<uint32_t> &&animatedIndices, std::vector<glm::mat3x4> &&transforms,
        std::vector<Geometry> &&geometries, std::vector<Shaders::Material> &&materials,
        std::vector<TextureInfo> &&textures, std::vector<Model> &&models,
        std::vector<ModelInstance> &&modelInstances, std::vector<Bone> &&bones, SceneGraph &&sceneGraph,
        std::vector<LightInfo> &&lightInfos, std::vector<Shaders::Light> &&lights, SkyboxVariant &&skybox,
        const std::vector<CameraInfo> &cameraInfos
    );

    [[nodiscard]] const std::string &GetName() const;

    void Update(float timeStep);

    [[nodiscard]] std::span<const Shaders::Vertex> GetVertices() const;
    [[nodiscard]] std::span<const Shaders::AnimatedVertex> GetAnimatedVertices() const;
    [[nodiscard]] std::span<const uint32_t> GetIndices() const;
    [[nodiscard]] std::span<const uint32_t> GetAnimatedIndices() const;
    [[nodiscard]] std::span<const glm::mat3x4> GetTransforms() const;
    [[nodiscard]] std::span<const Geometry> GetGeometries() const;
    [[nodiscard]] std::span<const Shaders::Material> GetMaterials() const;
    [[nodiscard]] std::span<const TextureInfo> GetTextures() const;

    [[nodiscard]] std::span<const Model> GetModels() const;
    [[nodiscard]] std::span<const ModelInstance> GetModelInstances() const;

    [[nodiscard]] std::span<const glm::mat3x4> GetBoneTransforms() const;

    [[nodiscard]] bool HasAnimations() const;
    [[nodiscard]] bool HasSkeletalAnimations() const;

    [[nodiscard]] std::span<const Shaders::Light> GetLights() const;

    [[nodiscard]] const SkyboxVariant &GetSkybox() const;

    [[nodiscard]] uint32_t GetSceneCamerasCount() const;
    [[nodiscard]] CameraId GetActiveCameraId() const;
    [[nodiscard]] Camera &GetActiveCamera();
    void SetActiveCamera(CameraId id);

    inline static const CameraId g_InputCameraId = -1;

public:
    [[nodiscard]] static uint32_t GetDefaultTextureIndex(TextureType type);

private:
    std::string m_Name;

    std::vector<Shaders::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<glm::mat3x4> m_Transforms;

    std::vector<Shaders::AnimatedVertex> m_AnimatedVertices;
    std::vector<uint32_t> m_AnimatedIndices;

    std::vector<Geometry> m_Geometries;

    std::vector<Shaders::Material> m_Materials;

    std::vector<TextureInfo> m_Textures;

    std::vector<Model> m_Models;
    std::vector<ModelInstance> m_ModelInstances;

    std::vector<Bone> m_Bones;
    std::vector<glm::mat3x4> m_BoneTransforms;

    SceneGraph m_Graph;
    bool m_HasSkeletalAnimations = false;
    
    std::vector<LightInfo> m_LightInfos;
    std::vector<Shaders::Light> m_Lights;

    SkyboxVariant m_Skybox = SkyboxClearColor {};

    InputCamera m_InputCamera =
        InputCamera(45.0f, 100.0f, 0.1f, glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    std::vector<AnimatedCamera> m_SceneCameras;
    CameraId m_ActiveCameraId = g_InputCameraId;
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
    uint32_t AddMaterial(std::string name, Shaders::Material material);

    void SetVertices(std::vector<Shaders::Vertex> &&vertices);
    void SetIndices(std::vector<uint32_t> &&indices);

    void SetAnimatedVertices(std::vector<Shaders::AnimatedVertex> &&vertices);
    void SetAnimatedIndices(std::vector<uint32_t> &&indices);

    uint32_t AddBone(Bone &&bone);
    void SetAbsoluteTransform(uint32_t sceneNodeIndex);

    void AddLight(Shaders::Light &&light, uint32_t sceneNodeIndex);

    void SetSkybox(Skybox2D &&skybox);
    void SetSkybox(SkyboxCube &&skybox);

    void AddCamera(CameraInfo &&camera);

    [[nodiscard]] std::shared_ptr<Scene> CreateSceneShared(std::string name);

public:
    static inline constexpr uint32_t IdentityTransformIndex = 0;

private:
    std::vector<Shaders::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<glm::mat3x4> m_Transforms = { glm::mat3x4(1.0f) };

    std::vector<Shaders::AnimatedVertex> m_AnimatedVertices;
    std::vector<uint32_t> m_AnimatedIndices;

    std::vector<Geometry> m_Geometries;

    std::vector<Shaders::Material> m_Materials;
    std::unordered_map<std::string, uint32_t> m_MaterialIndices;

    std::vector<TextureInfo> m_Textures;
    std::unordered_map<std::string, uint32_t> m_TextureIndices;

    std::vector<Model> m_Models;
    std::vector<std::pair<uint32_t, uint32_t>> m_ModelInstanceInfos;

    std::vector<SceneNode> m_SceneNodes;
    std::vector<bool> m_IsRelativeTransform;
    std::vector<Animation> m_Animations;

    std::vector<Bone> m_Bones;

    std::vector<Shaders::Light> m_Lights;
    std::vector<LightInfo> m_LightInfos;

    SkyboxVariant m_Skybox = SkyboxClearColor {};

    std::vector<CameraInfo> m_CameraInfos;

private:
    static inline const Shaders::Light g_DefaultLight = {
        .Color = glm::vec3(1.0f),
        .Position = glm::vec3(3.0f, 15.0f, 7.0f),
        .AttenuationConstant = 1.0f,
    };

private:
    uint32_t m_MeshOffset = 0;

    Model CreateModel(std::span<const MeshInfo> meshInfos);
};

}
