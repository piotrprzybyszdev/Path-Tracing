#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Core/Registry.h"

#include "Shaders/ShaderTypes.incl"

namespace PathTracing
{

enum class TextureType : uint8_t
{
    Color, Normal, Roughness, Metalic
};

struct TextureInfo
{
    TextureType Type;
    std::filesystem::path Path;
};

struct Material
{
    std::optional<std::filesystem::path> Color;
    std::optional<std::filesystem::path> Normal;
    std::optional<std::filesystem::path> Roughness;
    std::optional<std::filesystem::path> Metalic;
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
    glm::mat4 Transform;
};

struct SkyboxClearColor
{
};

struct Skybox2D
{
    std::filesystem::path Path;
};

struct SkyboxCube
{
    std::filesystem::path Front;
    std::filesystem::path Back;
    std::filesystem::path Up;
    std::filesystem::path Down;
    std::filesystem::path Left;
    std::filesystem::path Right;
};

class Scene
{
public:
    using SkyboxVariant = std::variant<SkyboxClearColor, Skybox2D, SkyboxCube>;

public:
    Scene();

    uint32_t AddGeometry(
        std::span<const Shaders::Vertex> vertices, std::span<const uint32_t> indices, bool IsOpaque
    );
    uint32_t AddModel(std::span<const MeshInfo> meshInfos);
    uint32_t AddModelInstance(uint32_t modelIndex, glm::mat4 transform);

    uint32_t AddMaterial(std::string name, Material material);

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
    std::vector<ModelInstance> m_ModelInstances;

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

private:
    uint32_t AddTexture(TextureType type, std::filesystem::path path);
};

}
