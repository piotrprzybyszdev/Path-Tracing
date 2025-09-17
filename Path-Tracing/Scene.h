#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Core/Registry.h"

#include "Shaders/ShaderTypes.incl"

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
};

struct MeshInfo
{
    uint32_t GeometryIndex;
    uint32_t MaterialIndex;
    glm::mat4 Transform;
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
    uint32_t SbtOffset;
};

struct ModelInstanceInfo
{
    uint32_t ModelIndex;
    uint32_t SceneNodeIndex;
};

struct ModelInstance
{
    uint32_t ModelIndex;
    glm::mat4 Transform;
};

struct SceneNode
{
    const uint32_t Parent;
    glm::mat4 Transform;
    glm::mat4 CurrentTransform;
};

struct AnimationNode
{
    template<typename T> struct Sequence
    {
        struct Key
        {
            T Value;
            float Tick;
        };

        std::vector<Key> Keys;
        uint32_t Index = 0;

        T Update(float currentTick);
        T Interpolate(float ratio);
    };

    uint32_t SceneNodeIndex;

    Sequence<glm::vec3> Positions;
    Sequence<glm::quat> Rotations;
    Sequence<glm::vec3> Scales;
};

template<typename T> inline T AnimationNode::Sequence<T>::Update(float currentTick)
{
    while (Index + 1 < Keys.size() && currentTick > Keys[Index + 1].Tick)
        Index++;

    if (Index + 1 == Keys.size())
        return Keys[Index].Value;

    if (Index == 0 && Keys[0].Tick > currentTick)
        return T();

    const float total = Keys[Index + 1].Tick - Keys[Index].Tick;
    const float current = currentTick - Keys[Index].Tick;

    return Interpolate(current / total);
}

template<typename T> inline T AnimationNode::Sequence<T>::Interpolate(float ratio)
{
    return glm::mix(Keys[Index].Value, Keys[Index + 1].Value, ratio);
}

template<> inline glm::quat AnimationNode::Sequence<glm::quat>::Interpolate(float ratio)
{
    return glm::slerp(Keys[Index].Value, Keys[Index + 1].Value, ratio);
}

struct Animation
{
    std::vector<AnimationNode> Nodes;
    const float TickPerSecond;
    const float TickDuration;
    float CurrentTick = 0;

    void Update(float timeStep, std::span<SceneNode> nodes);
};

/*
* TODO: For bone meshes
struct ModelInstanceDynamic
{
};
*/

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

class SceneGraph
{
public:
    uint32_t AddSceneNode(SceneNode &&node);
    void AddAnimation(Animation &&animation);

    void Update(float timeStep);

    [[nodiscard]] std::span<const SceneNode> GetSceneNodes() const;

private:
    std::vector<Animation> m_Animations;
    std::vector<SceneNode> m_SceneNodes;

private:
    void UpdateTransforms();
};

class Scene
{
public:
    using SkyboxVariant = std::variant<SkyboxClearColor, Skybox2D, SkyboxCube>;

public:
    Scene();

    void Update(float timeStep);

    /* Nodes have to be added in pre-order sequence */
    uint32_t AddSceneNode(SceneNode &&node);
    void AddAnimation(Animation &&animation);

    uint32_t AddGeometry(Geometry &&geometry);
    uint32_t AddModel(std::span<const MeshInfo> meshInfos);
    uint32_t AddModelInstance(ModelInstanceInfo &&info);

    uint32_t AddTexture(TextureInfo &&texture);
    uint32_t AddMaterial(std::string name, Shaders::Material material);

    void SetVertices(std::vector<Shaders::Vertex> &&vertices);
    void SetIndices(std::vector<uint32_t> &&indices);

    void SetSkybox(Skybox2D &&skybox);
    void SetSkybox(SkyboxCube &&skybox);

    [[nodiscard]] std::span<const Shaders::Vertex> GetVertices() const;
    [[nodiscard]] std::span<const uint32_t> GetIndices() const;
    [[nodiscard]] std::span<const glm::mat4> GetTransforms() const;
    [[nodiscard]] std::span<const Geometry> GetGeometries() const;
    [[nodiscard]] std::span<const Shaders::Material> GetMaterials() const;
    [[nodiscard]] std::span<const TextureInfo> GetTextures() const;

    [[nodiscard]] std::span<const Model> GetModels() const;
    [[nodiscard]] std::span<const ModelInstance> GetModelInstances() const;

    [[nodiscard]] const SkyboxVariant &GetSkybox() const;

    static inline constexpr uint32_t IdentityTransformIndex = 0;

private:
    std::vector<Shaders::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<glm::mat4> m_Transforms;

    std::vector<Geometry> m_Geometries;

    std::vector<Shaders::Material> m_Materials;
    std::unordered_map<std::string, uint32_t> m_MaterialIndices;

    std::vector<TextureInfo> m_Textures;
    std::unordered_map<std::string, uint32_t> m_TextureIndices;

    std::vector<Model> m_Models;
    std::vector<ModelInstanceInfo> m_ModelInstanceInfos;
    std::vector<ModelInstance> m_ModelInstances;

    SceneGraph m_Graph;

    SkyboxVariant m_Skybox = SkyboxClearColor {};

#ifndef NDEBUG
    static inline constexpr bool g_EnableNameRegistries = true;
#else
    static inline constexpr bool g_EnableNameRegistries = false;
#endif

    static inline const std::string g_DefaultGeometryName = "Unnamed Geometry";
    static inline const std::string g_DefaultModelName = "Unnamed Model";
    static inline const std::string g_DefaultModelInstanceName = "Unnamed Model Instance";

public:
    Registry<std::pair<uint32_t, uint32_t>, std::string, g_DefaultGeometryName, g_EnableNameRegistries>
        MeshNames;
    Registry<uint32_t, std::string, g_DefaultModelName, g_EnableNameRegistries> ModelNames;
    Registry<uint32_t, std::string, g_DefaultModelInstanceName, g_EnableNameRegistries> ModelInstanceNames;
};

}
