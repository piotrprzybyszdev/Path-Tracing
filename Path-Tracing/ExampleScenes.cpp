#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "Core/Core.h"

#include "Application.h"
#include "ExampleScenes.h"
#include "Resources.h"
#include "SceneImporter.h"
#include "TextureImporter.h"

namespace PathTracing::ExampleScenes
{

template<void(load)(SceneBuilder &)> class CustomSceneLoader : public SceneLoader
{
public:
    ~CustomSceneLoader() override = default;

    void Load(SceneBuilder &sceneBuilder) override;
};

void CreateDefaultScene(SceneBuilder &sceneBuilder);
void CreateMetalicRoughnessCubesScene(SceneBuilder &sceneBuilder);
void CreateReuseMeshCubesScene(SceneBuilder &sceneBuilder);
void CreateRoughnessTestCubesScene(SceneBuilder &sceneBuilder);

static SceneGroup &AddSceneGroup(std::map<std::string, SceneGroup> &scenes, const std::string &name)
{
    auto [it, _] = scenes.emplace(name, SceneGroup {});
    return it->second;
}

static void AddKhronosScenes(std::map<std::string, SceneGroup> &scenes)
{
    const std::filesystem::path base = Application::GetConfig().AssetDirectoryPath / "scenes" /
                                       "KhronosScenes" / "glTF-Sample-Assets-main" / "Models";
    SceneGroup &group = AddSceneGroup(scenes, "Khronos Scenes");

    for (const auto &entry : std::filesystem::recursive_directory_iterator(base))
    {
        try
        {
            if (entry.path().extension() != ".gltf")
                continue;

            auto loader = std::make_unique<CombinedSceneLoader>();
            loader->AddComponent(entry.path());

            group.emplace(entry.path().stem().string(), std::move(loader));
        }
        catch (const std::exception &exc)
        {
            logger::debug("Error when iterating scene folders: {}", exc.what());
        }
    }
}

struct SceneDescription
{
    std::vector<std::filesystem::path> ComponentPaths;
    std::optional<std::filesystem::path> SkyboxPath;
    TextureMapping Mapping;

    [[nodiscard]] std::unique_ptr<CombinedSceneLoader> ToLoader() const;
};

std::unique_ptr<CombinedSceneLoader> SceneDescription::ToLoader() const
{
    auto loader = std::make_unique<CombinedSceneLoader>();
    loader->AddTextureMapping(Mapping);

    for (const auto &path : ComponentPaths)
    {
        if (std::filesystem::exists(path))
            loader->AddComponent(path);
        else
            logger::warn("Scene component not found: {}", path.string());
    }

    if (SkyboxPath.has_value())
    {
        if (std::filesystem::exists(SkyboxPath.value()))
            loader->AddSkybox2D(SkyboxPath.value());
        else
            logger::warn("Skybox file not found: {}", SkyboxPath.value().string());
    }

    return loader;
}

static void AddSceneByDescription(
    SceneGroup &sceneGroup, const std::string &name, SceneDescription &&description
)
{
    auto loader = description.ToLoader();
    if (loader->HasContent())
        sceneGroup.emplace(name, std::move(loader));
    else
        logger::warn("Entire scene {} not found", name);
}

static void AddHighQualityScenes(std::map<std::string, SceneGroup> &scenes)
{
    const std::filesystem::path base = Application::GetConfig().AssetDirectoryPath / "scenes";
    SceneGroup &group = AddSceneGroup(scenes, "High Quality Scenes");

    SceneDescription intelSponzaDescription = {
        .ComponentPaths = {
            base / "IntelSponzaMain" / "main_sponza" / "NewSponza_Main_glTF_003.gltf",
            base / "IntelSponzaCurtains" / "pkg_a_curtains" / "NewSponza_Curtains_glTF.gltf",
            base / "IntelSponzaIvy" / "pkg_b_ivy" / "NewSponza_IvyGrowth_glTF.gltf",
        },
        .SkyboxPath = base / "IntelSponzaMain" / "main_sponza" / "textures" / "kloppenheim_05_4k.hdr",
    };

    /* NOTE:
     * Scenes from NVIDIA Orca collection have specular textures
     * that get picked up by assimp as the exponent in the phong lighting model
     * However, they make no sense as such
     * You might think that they must be the specular color textures
     * It would make sense since they are BC1 encoded
     * However, the red channel seems to always be 0
     * You might think that they are the specular/glossiness textures with
     * specular encoded as a scalar intensity instead of a 3-component color
     * However, the values still don't make sense
     * The values do make sense however when the they are interpreted as roughness/metalness
     *
     * Hence - The need for the below mapping
     */
    static const MetalicRoughnessTextureMapping NVIDIAOrcaTextureMapping = {
        .ColorTexture = TextureType::Color,
        .NormalTexture = TextureType::Normal,
        .RoughnessTexture = TextureType::Specular,
        .MetalicTexture = TextureType::Specular,
    };

    SceneDescription ue4SunTempleDescription = {
        .ComponentPaths = { base / "UE4SunTemple" / "SunTemple_v4" / "SunTemple" / "SunTemple.fbx" },
        .SkyboxPath = { base / "UE4SunTemple" / "SunTemple_v4" / "SunTemple" / "SunTemple_Skybox.hdr" },
        .Mapping = NVIDIAOrcaTextureMapping,
    };

    SceneDescription amazonBistroDescription = {
        .ComponentPaths = {
            base / "AmazonBistro" / "Bistro_v5_2" / "BistroExterior.fbx",
            base / "AmazonBistro" / "Bistro_v5_2" / "BistroInterior.fbx",
        },
        .SkyboxPath = base / "AmazonBistro" / "Bistro_v5_2" / "san_giuseppe_bridge_4k.hdr",
        .Mapping = NVIDIAOrcaTextureMapping,
    };

    SceneDescription amazonBistroWineDescription = {
        .ComponentPaths = {
            base / "AmazonBistro" / "Bistro_v5_2" / "BistroExterior.fbx",
            base / "AmazonBistro" / "Bistro_v5_2" / "BistroInterior_Wine.fbx",
        },
        .SkyboxPath = base / "AmazonBistro" / "Bistro_v5_2" / "san_giuseppe_bridge_4k.hdr",
        .Mapping = NVIDIAOrcaTextureMapping,
    };

    AddSceneByDescription(group, "Intel Sponza", std::move(intelSponzaDescription));
    AddSceneByDescription(group, "UE4 Sun Temple", std::move(ue4SunTempleDescription));
    AddSceneByDescription(group, "Amazon Bistro", std::move(amazonBistroDescription));
    AddSceneByDescription(group, "Amazon Bistro Wine", std::move(amazonBistroWineDescription));
}

static void AddTestScenes(std::map<std::string, SceneGroup> &scenes)
{
    SceneGroup &group = AddSceneGroup(scenes, "Test Scenes");
    group.emplace(
        "Roughness Test Cubes", std::make_unique<CustomSceneLoader<CreateRoughnessTestCubesScene>>()
    );
    group.emplace(
        "MetalicRoughness Cubes", std::make_unique<CustomSceneLoader<CreateMetalicRoughnessCubesScene>>()
    );
    group.emplace("Reuse Mesh", std::make_unique<CustomSceneLoader<CreateMetalicRoughnessCubesScene>>());
    group.emplace("Default", std::make_unique<CustomSceneLoader<CreateDefaultScene>>());
}

void AddScenes(std::map<std::string, SceneGroup> &scenes)
{
    scenes.clear();
    AddTestScenes(scenes);
    AddKhronosScenes(scenes);
    AddHighQualityScenes(scenes);
}

template<void(load)(SceneBuilder &sceneBuilder)>
void CustomSceneLoader<load>::Load(SceneBuilder &sceneBuilder)
{
    return load(sceneBuilder);
}

static std::array<uint32_t, 6> AddCube(SceneBuilder &sceneBuilder)
{
    auto &vertices = sceneBuilder.GetVertices();
    uint32_t vertexOffset = vertices.size();
    std::ranges::copy(
        std::array<Shaders::Vertex, 24> { {
            { { -1, -1, 1 }, { 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
            { { 1, -1, 1 }, { 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
            { { 1, 1, 1 }, { 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
            { { -1, 1, 1 }, { 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },

            { { 1, -1, -1 }, { 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
            { { -1, -1, -1 }, { 1, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
            { { -1, 1, -1 }, { 1, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
            { { 1, 1, -1 }, { 0, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },

            { { -1, -1, -1 }, { 0, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
            { { -1, -1, 1 }, { 1, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
            { { -1, 1, 1 }, { 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
            { { -1, 1, -1 }, { 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },

            { { 1, -1, 1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
            { { 1, -1, -1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
            { { 1, 1, -1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
            { { 1, 1, 1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },

            { { -1, 1, 1 }, { 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
            { { 1, 1, 1 }, { 1, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
            { { 1, 1, -1 }, { 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
            { { -1, 1, -1 }, { 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },

            { { -1, -1, -1 }, { 0, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
            { { 1, -1, -1 }, { 1, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
            { { 1, -1, 1 }, { 1, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
            { { -1, -1, 1 }, { 0, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        } },
        std::back_inserter(vertices)
    );

    auto &indices = sceneBuilder.GetIndices();
    uint32_t indexOffset = indices.size();
    for (int i = 0; i < 6; i++)
        std::ranges::copy(std::array<uint32_t, 6> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    std::array<uint32_t, 6> geometryIndices = {};
    for (uint32_t i = 0; i < 6; i++)
    {
        geometryIndices[i] = sceneBuilder.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
        vertexOffset += 4;
        indexOffset += 6;
    }

    return geometryIndices;
}

void CreateDefaultScene(SceneBuilder &sceneBuilder)
{
    auto makeMaterialFromColor = [](glm::vec3 color, float roughness = 1.0f) {
        return Shaders::MetalicRoughnessMaterial {
            .Color = color,
            .Roughness = roughness,
            .Metalness = 0.0f,
            .EmissiveIdx = Scene::GetDefaultTextureIndex(TextureType::Emisive),
            .ColorIdx = Scene::GetDefaultTextureIndex(TextureType::Color),
            .NormalIdx = Scene::GetDefaultTextureIndex(TextureType::Normal),
            .RoughnessIdx = Scene::GetDefaultTextureIndex(TextureType::Roughness),
            .MetalicIdx = Scene::GetDefaultTextureIndex(TextureType::Metalic),
        };
    };
    auto makeMaterialFromEmissiveColor = [](glm::vec3 color) {
        return Shaders::MetalicRoughnessMaterial {
            .EmissiveColor = color,
            .EmissiveIntensity = 1.0f,
            .Roughness = 1.0f,
            .Metalness = 0.0f,
            .EmissiveIdx = Scene::GetDefaultTextureIndex(TextureType::Emisive),
            .ColorIdx = Scene::GetDefaultTextureIndex(TextureType::Color),
            .NormalIdx = Scene::GetDefaultTextureIndex(TextureType::Normal),
            .RoughnessIdx = Scene::GetDefaultTextureIndex(TextureType::Roughness),
            .MetalicIdx = Scene::GetDefaultTextureIndex(TextureType::Metalic),
        };
    };
    auto makeMaterialFromTexture = [&](std::span<const uint8_t>) {
        return Shaders::MetalicRoughnessMaterial {
            .Color = glm::vec3(1.0f),
            .Roughness = 1.0f,
            .Metalness = 0.0f,
            .EmissiveIdx = Scene::GetDefaultTextureIndex(TextureType::Emisive),
            .ColorIdx = sceneBuilder.AddTexture(
                TextureImporter::GetTextureInfo(
                    Resources::g_PlaceholderTextureData, TextureType::Color, "Logo Texture"
                )
            ),
            .NormalIdx = Scene::GetDefaultTextureIndex(TextureType::Normal),
            .RoughnessIdx = Scene::GetDefaultTextureIndex(TextureType::Roughness),
            .MetalicIdx = Scene::GetDefaultTextureIndex(TextureType::Metalic),
        };
    };

    Shaders::MaterialId whiteMaterial =
        sceneBuilder.AddMaterial("White Material", makeMaterialFromColor(glm::vec3(1.0f)));
    Shaders::MaterialId greenMaterial =
        sceneBuilder.AddMaterial("Green Material", makeMaterialFromColor(glm::vec3(0.0f, 1.0f, 0.0f), 0.1f));
    Shaders::MaterialId redMaterial =
        sceneBuilder.AddMaterial("Red Material", makeMaterialFromColor(glm::vec3(1.0f, 0.0f, 0.0f), 0.1f));
    Shaders::MaterialId logoMaterial = sceneBuilder.AddMaterial(
        "Logo Material", makeMaterialFromTexture(Resources::g_PlaceholderTextureData)
    );
    Shaders::MaterialId lightMaterial =
        sceneBuilder.AddMaterial("Light Material", makeMaterialFromEmissiveColor(glm::vec3(1.0f)));

    auto &vertices = sceneBuilder.GetVertices();
    vertices = {
        { { -1.1, -1.1, -1 }, { 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1.1, -1.1, -1 }, { 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1.1, 1.1, -1 }, { 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { -1.1, 1.1, -1 }, { 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },

        { { 1.1, -1.1, 1 }, { 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { -1.1, -1.1, 1 }, { 1, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { -1.1, 1.1, 1 }, { 1, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { 1.1, 1.1, 1 }, { 0, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },

        { { -1.1, -1.1, 1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { -1.1, -1.1, -1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { -1.1, 1.1, -1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { -1.1, 1.1, 1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },

        { { -1.1, -1.1, 1 }, { 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1.1, -1.1, 1 }, { 1, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1.1, -1.1, -1 }, { 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { -1.1, -1.1, -1 }, { 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },

        { { -1.1, 1.1, -1 }, { 0, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { 1.1, 1.1, -1 }, { 1, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { 1.1, 1.1, 1 }, { 1, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { -1.1, 1.1, 1 }, { 0, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
    };

    auto &indices = sceneBuilder.GetIndices();
    for (int i = 0; i < 5; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    uint32_t vertexOffset = 0, indexOffset = 0;
    for (uint32_t i = 0; i < 5; i++)
    {
        sceneBuilder.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
        vertexOffset += 4;
        indexOffset += 6;
    }

    std::array<MeshInfo, 5> meshes = { {
        { 0, redMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { 1, greenMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { 2, logoMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { 3, whiteMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { 4, whiteMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
    } };

    std::array<uint32_t, 6> geometryIndices = AddCube(sceneBuilder);

    std::array<MeshInfo, 6> cubeMeshes = { {
        { geometryIndices[0], whiteMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[1], whiteMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[2], whiteMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[3], whiteMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[4], whiteMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[5], whiteMaterial, MaterialType::MetalicRoughness, glm::mat4(1.0f) },
    } };

    const uint32_t lightVertexOffset = vertices.size();
    const uint32_t lightIndexOffset = indices.size();

    std::ranges::copy(
        std::array<Shaders::Vertex, 4> {
            Shaders::Vertex {
                { 0.2f, 0.0f, 0.2f }, { 1.0f, 1.0f }, { 0.0f, -1.0f, 0.0f }, { 1, 0, 0 }, { 0, 0, 1 } },
            Shaders::Vertex {
                { -0.2f, 0.0f, 0.2f }, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f }, { 1, 0, 0 }, { 0, 0, 1 } },
            Shaders::Vertex {
                { -0.2f, 0.0f, -0.2f }, { 0.0f, 1.0f }, { 0.0f, -1.0f, 0.0f }, { 1, 0, 0 }, { 0, 0, 1 } },
            Shaders::Vertex {
                { 0.2f, 0.0f, -0.2f }, { 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 1, 0, 0 }, { 0, 0, 1 } },
        },
        std::back_inserter(vertices)
    );
    std::ranges::copy(std::array<uint32_t, 6> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    const uint32_t lightGeometry =
        sceneBuilder.AddGeometry({ lightVertexOffset, 4, lightIndexOffset, 6, true });

    const std::array<MeshInfo, 1> lightMeshes = {
        MeshInfo {
            .GeometryIndex = lightGeometry,
            .MaterialIndex = lightMaterial,
            .ShaderMaterialType = MaterialType::MetalicRoughness,
            .Transform = glm::mat4(1.0f),
        },
    };

    const uint32_t box = sceneBuilder.AddModel(meshes);
    const uint32_t cube = sceneBuilder.AddModel(cubeMeshes);
    const uint32_t light = sceneBuilder.AddModel({ lightMeshes });

    const glm::mat4 boxTransform = glm::transpose(
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(2.0f)), glm::vec3(-1.9f, 0.5f, 0.0f))
    );

    const uint32_t rootNode = sceneBuilder.AddSceneNode({ 0u, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t boxNode = sceneBuilder.AddSceneNode({ rootNode, boxTransform, glm::mat4(1.0f) });

    const uint32_t boxInstance = sceneBuilder.AddModelInstance(box, boxNode);

    const glm::mat4 leftCubeTransform = glm::transpose(
        glm::scale(
            glm::rotate(
                glm::translate(glm::mat4(1.0f), glm::vec3(-0.4f, -0.8f, 0.5f)), glm::radians(25.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            ),
            glm::vec3(0.3f)
        )
    );
    const uint32_t leftCubeNode = sceneBuilder.AddSceneNode({ boxNode, leftCubeTransform, glm::mat4(1.0f) });

    const glm::mat4 rightCubeTransform = glm::transpose(
        glm::scale(
            glm::rotate(
                glm::translate(glm::mat4(1.0f), glm::vec3(0.2f, -0.8f, -0.6f)), glm::radians(-20.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            ),
            glm::vec3(0.3f)
        )
    );
    const uint32_t rightCubeNode =
        sceneBuilder.AddSceneNode({ boxNode, rightCubeTransform, glm::mat4(1.0f) });

    const uint32_t leftCubeInstance = sceneBuilder.AddModelInstance(cube, leftCubeNode);
    const uint32_t rightCubeInstance = sceneBuilder.AddModelInstance(cube, rightCubeNode);

    const glm::mat4 lightTransform =
        glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.099f, 0.0f)));
    const uint32_t lightNode = sceneBuilder.AddSceneNode({ boxNode, lightTransform, glm::mat4(1.0f) });
    sceneBuilder.AddModelInstance(light, lightNode);
}

void CreateMetalicRoughnessCubesScene(SceneBuilder &sceneBuilder)
{
    const std::filesystem::path base = Application::GetConfig().AssetDirectoryPath / "textures";
    const std::array<std::string, 3> assetNames = { "Metal", "PavingStones", "Logs" };
    const std::array<std::string, 3> materials = {
        "Metal062C_1K-JPG",
        "PavingStones142_1K-JPG",
        "Logs001_1K-JPG",
    };
    std::array<Shaders::MaterialId, 3> materialIds = {};

    auto addTexture = [&](const std::filesystem::path &materialPath, const std::string &texture,
                          TextureType type) {
        return sceneBuilder.AddTexture(
            TextureImporter::GetTextureInfo(materialPath / texture, type, std::string(texture))
        );
    };

    for (int i = 0; i < 3; i++)
    {
        const std::filesystem::path materialPath = base / assetNames[i];
        const std::string &material = materials[i];
        materialIds[i] = sceneBuilder.AddMaterial(
            assetNames[i],
            Shaders::MetalicRoughnessMaterial {
                .Color = glm::vec3(1.0f),
                .Roughness = 1.0f,
                .Metalness = 1.0f,
                .ColorIdx = addTexture(materialPath, material + "_Color.jpg", TextureType::Color),
                .NormalIdx = addTexture(materialPath, material + "_NormalGL.jpg", TextureType::Normal),
                .RoughnessIdx = addTexture(materialPath, material + "_Roughness.jpg", TextureType::Roughness),
                .MetalicIdx = addTexture(materialPath, material + "_Roughness.jpg", TextureType::Metalic),
            }
        );
    }

    std::array<uint32_t, 6> geometryIndices = AddCube(sceneBuilder);

    std::array<MeshInfo, 6> m1 = { {
        { geometryIndices[0], materialIds[0], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[1], materialIds[0], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[2], materialIds[1], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[3], materialIds[1], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[4], materialIds[2], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[5], materialIds[2], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
    } };

    std::array<MeshInfo, 6> m2 = { {
        { geometryIndices[0], materialIds[0], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[1], materialIds[0], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[2], materialIds[0], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[3], materialIds[0], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[4], materialIds[0], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[5], materialIds[0], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
    } };

    const uint32_t cube1 = sceneBuilder.AddModel(m1);
    const uint32_t cube2 = sceneBuilder.AddModel(m2);

    const glm::mat4 cube1inst1transform = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f)));
    const glm::mat4 cube1inst2transform = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f)));
    const glm::mat4 cube2transform = glm::transpose(
        glm::scale(
            glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, -1.0f, -3.0f)), glm::vec3(2.0f, 1.0f, 0.3f)
        )
    );

    const uint32_t rootNode = sceneBuilder.AddSceneNode({ 0u, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1inst1node =
        sceneBuilder.AddSceneNode({ rootNode, cube1inst1transform, glm::mat4(1.0f) });
    const uint32_t cube1inst2node =
        sceneBuilder.AddSceneNode({ rootNode, cube1inst2transform, glm::mat4(1.0f) });
    const uint32_t cube2node = sceneBuilder.AddSceneNode({ rootNode, cube2transform, glm::mat4(1.0f) });

    const uint32_t cube1inst1 = sceneBuilder.AddModelInstance(cube1, cube1inst1node);
    const uint32_t cube1inst2 = sceneBuilder.AddModelInstance(cube1, cube1inst2node);
    const uint32_t cube2inst = sceneBuilder.AddModelInstance(cube2, cube2node);

    const uint32_t lightNode = sceneBuilder.AddSceneNode(
        { rootNode, glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f, 3.0f, 0.0f))),
          glm::mat4(1.0f) }
    );

    sceneBuilder.AddLight(
        {
            .Color = glm::vec3(1.0f),
            .Position = glm::vec3(0.0f),
            .AttenuationConstant = 0.0f,
            .AttenuationLinear = 0.0f,
            .AttenuationQuadratic = 1.0f,
        },
        lightNode
    );

    AnimationNode animNode = { .SceneNodeIndex = lightNode };
    animNode.Positions.Keys = {
        { glm::vec3(-1.0f, 3.0f, 0.0f), 0.0f },
        { glm::vec3(1.0f, 3.0f, 0.0f), 90.0f },
        { glm::vec3(-1.0f, 3.0f, 0.0f), 180.0f },
    };
    animNode.Rotations.Keys = { { glm::quat(), 0.0f } };
    animNode.Scales.Keys = { { glm::vec3(1.0f), 0.0f } };

    sceneBuilder.AddAnimation(Animation({ animNode }, 30.0f, 180.0f));

    sceneBuilder.SetSkybox(Skybox2D(
        TextureImporter::GetTextureInfo(base / "skybox" / "sky_42_2k.png", TextureType::Skybox, "Skybox")
    ));
}

void CreateReuseMeshCubesScene(SceneBuilder &sceneBuilder)
{
    const std::filesystem::path base = Application::GetConfig().AssetDirectoryPath / "textures";
    const std::array<std::string, 3> assetNames = { "Metal", "PavingStones", "Logs" };
    const std::array<std::string, 3> materials = {
        "Metal062C_1K-JPG",
        "PavingStones142_1K-JPG",
        "Logs001_1K-JPG",
    };
    std::array<Shaders::MaterialId, 3> materialIds = {};

    auto addTexture = [&](const std::filesystem::path &materialPath, const std::string &texture,
                          TextureType type) {
        return sceneBuilder.AddTexture(
            TextureImporter::GetTextureInfo(materialPath / texture, type, std::string(texture))
        );
    };

    for (int i = 0; i < 3; i++)
    {
        const std::filesystem::path materialPath = base / assetNames[i];
        const std::string &material = materials[i];
        materialIds[i] = sceneBuilder.AddMaterial(
            assetNames[i],
            Shaders::MetalicRoughnessMaterial {
                .Color = glm::vec3(1.0f),
                .Roughness = 1.0f,
                .Metalness = 1.0f,
                .ColorIdx = addTexture(materialPath, material + "_Color.jpg", TextureType::Color),
                .NormalIdx = addTexture(materialPath, material + "_NormalGL.jpg", TextureType::Normal),
                .RoughnessIdx = addTexture(materialPath, material + "_Roughness.jpg", TextureType::Roughness),
                .MetalicIdx = addTexture(materialPath, material + "_Roughness.jpg", TextureType::Metalic),
            }
        );
    }

    auto &vertices = sceneBuilder.GetVertices();
    vertices = {
        { { -1, -1, 1 }, { 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1, -1, 1 }, { 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1, 1, 1 }, { 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { -1, 1, 1 }, { 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },

        { { -1, -1, -1 }, { 0, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, -1, 1 }, { 1, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, 1, 1 }, { 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, 1, -1 }, { 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },

        { { -1, 1, 1 }, { 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1, 1, 1 }, { 1, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1, 1, -1 }, { 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { -1, 1, -1 }, { 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
    };

    auto &indices = sceneBuilder.GetIndices();
    for (int i = 0; i < 3; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    uint32_t vertexOffset = 0, indexOffset = 0;
    std::array<uint32_t, 3> geometryIndices = {};
    for (uint32_t i = 0; i < 3; i++)
    {
        geometryIndices[i] = sceneBuilder.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
        vertexOffset += 4;
        indexOffset += 6;
    }

    std::array<MeshInfo, 6> m = { {
        { geometryIndices[0], materialIds[1], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[0], materialIds[1], MaterialType::MetalicRoughness,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f))) },
        { geometryIndices[1], materialIds[1], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[1], materialIds[2], MaterialType::MetalicRoughness,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f))) },
        { geometryIndices[2], materialIds[2], MaterialType::MetalicRoughness, glm::mat4(1.0f) },
        { geometryIndices[2], materialIds[2], MaterialType::MetalicRoughness,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f))) },
    } };

    const uint32_t cube = sceneBuilder.AddModel(m);

    const uint32_t rootNode = sceneBuilder.AddSceneNode({ 0u, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1node = sceneBuilder.AddSceneNode({ rootNode, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1inst = sceneBuilder.AddModelInstance(cube, cube1node);

    const auto skyboxPath = base / "skybox" / "sky_42_cubemap_(roblox)_2k";
    sceneBuilder.SetSkybox(SkyboxCube(
        TextureImporter::GetTextureInfo(skyboxPath / "px.png", TextureType::Skybox, "Skybox px"),
        TextureImporter::GetTextureInfo(skyboxPath / "nx.png", TextureType::Skybox, "Skybox nx"),
        TextureImporter::GetTextureInfo(skyboxPath / "py.png", TextureType::Skybox, "Skybox py"),
        TextureImporter::GetTextureInfo(skyboxPath / "ny.png", TextureType::Skybox, "Skybox ny"),
        TextureImporter::GetTextureInfo(skyboxPath / "pz.png", TextureType::Skybox, "Skybox pz"),
        TextureImporter::GetTextureInfo(skyboxPath / "nz.png", TextureType::Skybox, "Skybox nz")
    ));
}

void CreateRoughnessTestCubesScene(SceneBuilder &sceneBuilder)
{
    const std::filesystem::path base = Application::GetConfig().AssetDirectoryPath / "textures";

    auto makeMaterialFromColor = [](glm::vec3 color, float roughness, float metalness) {
        return Shaders::MetalicRoughnessMaterial
        {
            .Color = color,
            .Roughness = roughness,
            .Metalness = metalness,
            .EmissiveIdx = Scene::GetDefaultTextureIndex(TextureType::Emisive),
            .ColorIdx = Scene::GetDefaultTextureIndex(TextureType::Color),
            .NormalIdx = Scene::GetDefaultTextureIndex(TextureType::Normal),
            .RoughnessIdx = Scene::GetDefaultTextureIndex(TextureType::Roughness),
            .MetalicIdx = Scene::GetDefaultTextureIndex(TextureType::Metalic),
        };
    };
    auto makeMaterialFromTexture = [&](std::span<const uint8_t>) {
        return Shaders::MetalicRoughnessMaterial {
            .Color = glm::vec3(1.0f),
            .Roughness = 1.0f,
            .Metalness = 0.0f,
            .EmissiveIdx = Scene::GetDefaultTextureIndex(TextureType::Emisive),
            .ColorIdx = sceneBuilder.AddTexture(
                TextureImporter::GetTextureInfo(
                    Resources::g_PlaceholderTextureData, TextureType::Color, "Logo Texture"
                )
            ),
            .NormalIdx = Scene::GetDefaultTextureIndex(TextureType::Normal),
            .RoughnessIdx = Scene::GetDefaultTextureIndex(TextureType::Roughness),
            .MetalicIdx = Scene::GetDefaultTextureIndex(TextureType::Metalic),
        };
    };

    std::array<std::array<Shaders::MaterialId, 6>, 6> whiteMaterials;
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 6; j++)
            whiteMaterials[i][j] = sceneBuilder.AddMaterial(
                "White Material " + std::to_string(i) + "_" + std::to_string(j),
                makeMaterialFromColor(glm::vec3(1.0f), i * 0.2f, j * 0.2f)
            );

    std::array<uint32_t, 6> geometryIndices = AddCube(sceneBuilder);

    std::array<std::array<MeshInfo, 6>, 36> cubeMeshes;
    for (int i = 0; i < 6; i++)
    {
        for (int j = 0; j < 6; j++)
        {
            for (int k = 0; k < 6; k++)
            {
                cubeMeshes[i * 6 + j][k] = MeshInfo {
                    .GeometryIndex = geometryIndices[k],
                    .MaterialIndex = whiteMaterials[i][j],
                    .ShaderMaterialType = MaterialType::MetalicRoughness,
                    .Transform = glm::mat4(1.0f),
                };
            }
        }
    }

    std::array<uint32_t, 36> cubeModels;
    for (int i = 0; i < 36; i++)
    {
        cubeModels[i] = sceneBuilder.AddModel(cubeMeshes[i]);
    }

    const uint32_t rootNode = sceneBuilder.AddSceneNode({ 0u, glm::mat4(1.0f), glm::mat4(1.0f) });
    std::array<glm::mat4, 36> cubeNodeTransforms;
    for (int i = 0; i < 6; i++)
    {
        for (int j = 0; j < 6; j++)
        {
            cubeNodeTransforms[i * 6 + j] = glm::transpose(
                glm::translate(glm::mat4(1.0f), glm::vec3(j * -4.0f, 0.0f, i * -4.0f))
            );
            const uint32_t cubeNode =
                sceneBuilder.AddSceneNode({ rootNode, cubeNodeTransforms[i * 6 + j], glm::mat4(1.0f) });
            sceneBuilder.AddModelInstance(cubeModels[i * 6 + j], cubeNode);
        }
    }

    sceneBuilder.SetSkybox(Skybox2D(
        TextureImporter::GetTextureInfo(base / "skybox" / "sky_42_2k.png", TextureType::Skybox, "Skybox")
    ));
}

}