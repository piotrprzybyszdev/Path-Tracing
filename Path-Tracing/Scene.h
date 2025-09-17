#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

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

struct ModelInstance
{
    uint32_t ModelIndex;
    uint32_t SceneNodeIndex;
    glm::mat4 Transform;
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

using SkyboxVariant = std::variant<SkyboxClearColor, Skybox2D, SkyboxCube>;

class Scene
{
public:
    Scene(
        std::string name, std::vector<Shaders::Vertex> &&vertices, std::vector<uint32_t> &&indices,
        std::vector<glm::mat4> &&transforms, std::vector<Geometry> &&geometries,
        std::vector<Shaders::Material> &&materials, std::vector<TextureInfo> &&textures,
        std::vector<Model> &&models, std::vector<ModelInstance> &&modelInstances, SceneGraph &&sceneGraph,
        SkyboxVariant &&skybox
    );

    [[nodiscard]] const std::string &GetName() const;

    void Update(float timeStep);

    [[nodiscard]] std::span<const Shaders::Vertex> GetVertices() const;
    [[nodiscard]] std::span<const uint32_t> GetIndices() const;
    [[nodiscard]] std::span<const glm::mat4> GetTransforms() const;
    [[nodiscard]] std::span<const Geometry> GetGeometries() const;
    [[nodiscard]] std::span<const Shaders::Material> GetMaterials() const;
    [[nodiscard]] std::span<const TextureInfo> GetTextures() const;

    [[nodiscard]] std::span<const Model> GetModels() const;
    [[nodiscard]] std::span<const ModelInstance> GetModelInstances() const;

    [[nodiscard]] bool HasAnimations() const;

    [[nodiscard]] const SkyboxVariant &GetSkybox() const;

private:
    std::string m_Name;

    std::vector<Shaders::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<glm::mat4> m_Transforms;

    std::vector<Geometry> m_Geometries;

    std::vector<Shaders::Material> m_Materials;
    std::unordered_map<std::string, uint32_t> m_MaterialIndices;

    std::vector<TextureInfo> m_Textures;
    std::unordered_map<std::string, uint32_t> m_TextureIndices;

    std::vector<Model> m_Models;
    std::vector<ModelInstance> m_ModelInstances;

    SceneGraph m_Graph;

    SkyboxVariant m_Skybox = SkyboxClearColor {};
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

    void SetSkybox(Skybox2D &&skybox);
    void SetSkybox(SkyboxCube &&skybox);

    [[nodiscard]] std::shared_ptr<Scene> CreateSceneShared(std::string name);

public:
    static inline constexpr uint32_t IdentityTransformIndex = 0;

private:
    std::vector<Shaders::Vertex> m_Vertices;
    std::vector<uint32_t> m_Indices;
    std::vector<glm::mat4> m_Transforms = { glm::mat4(1.0f) };

    std::vector<Geometry> m_Geometries;

    std::vector<Shaders::Material> m_Materials;
    std::unordered_map<std::string, uint32_t> m_MaterialIndices;

    std::vector<TextureInfo> m_Textures;
    std::unordered_map<std::string, uint32_t> m_TextureIndices;

    std::vector<Model> m_Models;
    std::vector<std::pair<uint32_t, uint32_t>> m_ModelInstanceInfos;
    std::vector<ModelInstance> m_ModelInstances;

    std::vector<SceneNode> m_SceneNodes;
    std::vector<Animation> m_Animations;

    SkyboxVariant m_Skybox = SkyboxClearColor {};
};

}
