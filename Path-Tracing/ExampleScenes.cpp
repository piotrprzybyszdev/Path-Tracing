#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>
#include <vector>

#include "Core/Core.h"

#include "Application.h"
#include "AssetImporter.h"
#include "ExampleScenes.h"

namespace PathTracing::ExampleScenes
{

template<void(load)(SceneBuilder &)> class CustomSceneLoader : public SceneLoader
{
public:
    ~CustomSceneLoader() override = default;

    void Load(SceneBuilder &sceneBuilder) override;
};

class CombinedSceneLoader : public SceneLoader
{
public:
    ~CombinedSceneLoader() override = default;

    void AddFile(const std::filesystem::path &path);
    void AddSkybox2D(const std::filesystem::path &path, bool isHDR);

    [[nodiscard]] bool HasContent() const;
    void Load(SceneBuilder &sceneBuilder) override;

private:
    std::vector<std::filesystem::path> m_FilePaths;
    std::filesystem::path m_SkyboxPath;
    bool m_IsSkyboxHDR;
    bool m_HasSkybox = false;
};

void CombinedSceneLoader::AddFile(const std::filesystem::path &path)
{
    m_FilePaths.push_back(path);
}

void CombinedSceneLoader::AddSkybox2D(const std::filesystem::path &path, bool isHDR)
{
    m_SkyboxPath = path;
    m_IsSkyboxHDR = isHDR;
    m_HasSkybox = true;
}

bool CombinedSceneLoader::HasContent() const
{
    return m_HasSkybox || !m_FilePaths.empty();
}

void CombinedSceneLoader::Load(SceneBuilder &sceneBuilder)
{
    for (const auto &path : m_FilePaths)
        AssetImporter::AddFile(sceneBuilder, path);

    if (!m_HasSkybox)
        return;

    TextureType type = m_IsSkyboxHDR ? TextureType::SkyboxHDR : TextureType::Skybox;
    sceneBuilder.SetSkybox(Skybox2D(AssetImporter::GetTextureInfo(
        Application::GetConfig().AssetDirectoryPath / "scenes" / m_SkyboxPath, type
    )));
}

void CreateTexturedCubesScene(SceneBuilder &sceneBuilder);
void CreateReuseMeshCubesScene(SceneBuilder &sceneBuilder);

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
            loader->AddFile(entry.path());

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
    std::filesystem::path SkyboxPath;
    bool HasSkybox = false;
    bool IsSkyboxHDR;

    [[nodiscard]] std::unique_ptr<CombinedSceneLoader> ToLoader() const;
};

std::unique_ptr<CombinedSceneLoader> SceneDescription::ToLoader() const
{
    auto loader = std::make_unique<CombinedSceneLoader>();
    for (const auto &path : ComponentPaths)
    {
        if (std::filesystem::exists(path))
            loader->AddFile(path);
        else
            logger::warn("Scene component not found: {}", path.string());
    }

    if (HasSkybox && std::filesystem::exists(SkyboxPath))
        loader->AddSkybox2D(SkyboxPath, IsSkyboxHDR);
    else
        logger::warn("Skybox file not found: {}", SkyboxPath.string());

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
        },
        .SkyboxPath = base / "IntelSponzaMain" / "main_sponza" / "textures" / "kloppenheim_05_4k.hdr",
        .HasSkybox = true,
        .IsSkyboxHDR = true,
    };

    AddSceneByDescription(group, "Intel Sponza", std::move(intelSponzaDescription));
}

static void AddTestScenes(std::map<std::string, SceneGroup> &scenes)
{
    SceneGroup &group = AddSceneGroup(scenes, "Test Scenes");
    group.emplace("Textured Cubes", std::make_unique<CustomSceneLoader<CreateTexturedCubesScene>>());
    group.emplace("Reuse Mesh", std::make_unique<CustomSceneLoader<CreateTexturedCubesScene>>());
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

void CreateTexturedCubesScene(SceneBuilder &sceneBuilder)
{
    const std::filesystem::path base = Application::GetConfig().AssetDirectoryPath / "textures";
    const std::array<std::string, 3> assetNames = { "Metal", "PavingStones", "Logs" };
    const std::array<std::string, 3> materials = {
        "Metal062C_1K-JPG",
        "PavingStones142_1K-JPG",
        "Logs001_1K-JPG",
    };

    for (int i = 0; i < 3; i++)
    {
        const std::filesystem::path materialPath = base / assetNames[i];
        const std::string &material = materials[i];
        sceneBuilder.AddMaterial(
            assetNames[i],
            Shaders::TexturedMaterial {
                sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(
                    materialPath / (material + "_Color.jpg"), TextureType::Color
                )),
                sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(
                    materialPath / (material + "_NormalGL.jpg"), TextureType::Normal
                )),
                sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(
                    materialPath / (material + "_Roughness.jpg"), TextureType::Roughness
                )),
                sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(
                    materialPath / (material + "_Roughness.jpg"), TextureType::Metalic
                )),
            }
        );
    }

    auto &vertices = sceneBuilder.GetVertices();
    vertices = {
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
    };

    auto &indices = sceneBuilder.GetIndices();
    for (int i = 0; i < 6; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    uint32_t vertexOffset = 0, indexOffset = 0;
    for (uint32_t i = 0; i < 6; i++)
    {
        sceneBuilder.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
        vertexOffset += 4;
        indexOffset += 6;
    }

    std::array<MeshInfo, 6> m1 = { {
        { 0, 0, MaterialType::Textured, glm::mat4(1.0f) },
        { 1, 0, MaterialType::Textured, glm::mat4(1.0f) },
        { 2, 1, MaterialType::Textured, glm::mat4(1.0f) },
        { 3, 1, MaterialType::Textured, glm::mat4(1.0f) },
        { 4, 2, MaterialType::Textured, glm::mat4(1.0f) },
        { 5, 2, MaterialType::Textured, glm::mat4(1.0f) },
    } };

    std::array<MeshInfo, 6> m2 = { {
        { 0, 0, MaterialType::Textured, glm::mat4(1.0f) },
        { 1, 0, MaterialType::Textured, glm::mat4(1.0f) },
        { 2, 0, MaterialType::Textured, glm::mat4(1.0f) },
        { 3, 0, MaterialType::Textured, glm::mat4(1.0f) },
        { 4, 0, MaterialType::Textured, glm::mat4(1.0f) },
        { 5, 0, MaterialType::Textured, glm::mat4(1.0f) },
    } };

    const uint32_t cube1 = sceneBuilder.AddModel(m1);
    const uint32_t cube2 = sceneBuilder.AddModel(m2);

    const glm::mat4 cube1inst1transform = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f)));
    const glm::mat4 cube1inst2transform = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f)));
    const glm::mat4 cube2transform = glm::transpose(glm::scale(
        glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, -1.0f, -3.0f)), glm::vec3(2.0f, 1.0f, 0.3f)
    ));

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

    sceneBuilder.SetSkybox(
        Skybox2D(AssetImporter::GetTextureInfo(base / "skybox" / "sky_42_2k.png", TextureType::Skybox))
    );
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

    for (int i = 0; i < 3; i++)
    {
        const std::filesystem::path materialPath = base / assetNames[i];
        const std::string &material = materials[i];
        sceneBuilder.AddMaterial(
            assetNames[i],
            Shaders::TexturedMaterial {
                sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(
                    materialPath / (material + "_Color.jpg"), TextureType::Color
                )),
                sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(
                    materialPath / (material + "_NormalGL.jpg"), TextureType::Normal
                )),
                sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(
                    materialPath / (material + "_Roughness.jpg"), TextureType::Roughness
                )),
                sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(
                    materialPath / (material + "_Roughness.jpg"), TextureType::Metalic
                )),
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
    for (uint32_t i = 0; i < 3; i++)
    {
        sceneBuilder.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
        vertexOffset += 4;
        indexOffset += 6;
    }

    std::array<MeshInfo, 6> m = { {
        { 0, 1, MaterialType::Textured, glm::mat4(1.0f) },
        { 0, 1, MaterialType::Textured,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f))) },
        { 1, 1, MaterialType::Textured, glm::mat4(1.0f) },
        { 1, 2, MaterialType::Textured,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f))) },
        { 2, 2, MaterialType::Textured, glm::mat4(1.0f) },
        { 2, 2, MaterialType::Textured,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f))) },
    } };

    const uint32_t cube = sceneBuilder.AddModel(m);

    const uint32_t rootNode = sceneBuilder.AddSceneNode({ 0u, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1node = sceneBuilder.AddSceneNode({ rootNode, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1inst = sceneBuilder.AddModelInstance(cube, cube1node);

    const auto skyboxPath = base / "skybox" / "sky_42_cubemap_(roblox)_2k";
    sceneBuilder.SetSkybox(SkyboxCube(
        AssetImporter::GetTextureInfo(skyboxPath / "px.png", TextureType::Skybox),
        AssetImporter::GetTextureInfo(skyboxPath / "nx.png", TextureType::Skybox),
        AssetImporter::GetTextureInfo(skyboxPath / "py.png", TextureType::Skybox),
        AssetImporter::GetTextureInfo(skyboxPath / "ny.png", TextureType::Skybox),
        AssetImporter::GetTextureInfo(skyboxPath / "pz.png", TextureType::Skybox),
        AssetImporter::GetTextureInfo(skyboxPath / "nz.png", TextureType::Skybox)
    ));
}

}