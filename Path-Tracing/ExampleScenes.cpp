#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

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

class FileSceneLoader : public SceneLoader
{
public:
    FileSceneLoader(const std::filesystem::path &path);
    ~FileSceneLoader() override = default;

    void Load(SceneBuilder &sceneBuilder) override;

private:
    std::filesystem::path m_Path;
};

FileSceneLoader::FileSceneLoader(const std::filesystem::path &path) : m_Path(path)
{
}

void FileSceneLoader::Load(SceneBuilder &sceneBuilder)
{
    AssetImporter::AddFile(sceneBuilder, Application::GetConfig().AssetFolderPath / "scenes" / m_Path);
}

class KhronosSceneLoader : public FileSceneLoader
{
public:
    KhronosSceneLoader(const std::string &name);
    ~KhronosSceneLoader() override = default;
};

KhronosSceneLoader::KhronosSceneLoader(const std::string &name)
    : FileSceneLoader(
          std::filesystem::path("KhronosScenes") / "glTF-Sample-Models-main" / "2.0" / name / "glTF" /
          (name + ".gltf")
      )
{
}

class Skybox2DLoader : public SceneLoader
{
public:
    Skybox2DLoader(const std::filesystem::path &path, bool isHDR);
    ~Skybox2DLoader() override = default;

    void Load(SceneBuilder &sceneBuilder) override;

private:
    std::filesystem::path m_Path;
    bool m_IsHDR;
};

Skybox2DLoader::Skybox2DLoader(const std::filesystem::path &path, bool isHDR) : m_Path(path), m_IsHDR(isHDR)
{
}

void Skybox2DLoader::Load(SceneBuilder &sceneBuilder)
{
    TextureType type = m_IsHDR ? TextureType::SkyboxHDR : TextureType::Skybox;
    sceneBuilder.SetSkybox(Skybox2D(
        AssetImporter::GetTextureInfo(Application::GetConfig().AssetFolderPath / "scenes" / m_Path, type)
    ));
}

class CombinedSceneLoader : public SceneLoader
{
public:
    void AddLoader(std::unique_ptr<SceneLoader> loader);

    void Load(SceneBuilder &sceneBuilder) override;

private:
    std::vector<std::unique_ptr<SceneLoader>> m_Loaders;
};

void CombinedSceneLoader::AddLoader(std::unique_ptr<SceneLoader> loader)
{
    m_Loaders.push_back(std::move(loader));
}

void CombinedSceneLoader::Load(SceneBuilder &sceneBuilder)
{
    for (const auto &loader : m_Loaders)
        loader->Load(sceneBuilder);
}

void CreateTexturedCubesScene(SceneBuilder &sceneBuilder);
void CreateReuseMeshCubesScene(SceneBuilder &sceneBuilder);

void AddScenes(std::map<std::string, std::unique_ptr<SceneLoader>> &scenes)
{
    // TODO: Find example scenes by searching the assets folder
    scenes.emplace("Textured Cubes", std::make_unique<CustomSceneLoader<CreateTexturedCubesScene>>());
    scenes.emplace("Reuse Mesh", std::make_unique<CustomSceneLoader<CreateTexturedCubesScene>>());
    scenes.emplace("Sponza", std::make_unique<KhronosSceneLoader>("Sponza"));
    scenes.emplace("Chess Game", std::make_unique<KhronosSceneLoader>("ABeautifulGame"));
    scenes.emplace("Virtual City", std::make_unique<KhronosSceneLoader>("VC"));
    scenes.emplace("Cesium Man", std::make_unique<KhronosSceneLoader>("CesiumMan"));
    scenes.emplace("Cesium Milk Truck", std::make_unique<KhronosSceneLoader>("CesiumMilkTruck"));
    scenes.emplace("Brain Stem", std::make_unique<KhronosSceneLoader>("BrainStem"));
    scenes.emplace("Box Animated", std::make_unique<KhronosSceneLoader>("BoxAnimated"));
    scenes.emplace("Lamp", std::make_unique<KhronosSceneLoader>("LightsPunctualLamp"));
    scenes.emplace("Rigged Simple", std::make_unique<KhronosSceneLoader>("RiggedSimple"));

    // TODO: Check if the packages are installed
    auto intelSponzaLoader = std::make_unique<CombinedSceneLoader>();
    intelSponzaLoader->AddLoader(std::make_unique<FileSceneLoader>(
        std::filesystem::path("main_sponza") / "NewSponza_Main_glTF_003.gltf"
    ));
    intelSponzaLoader->AddLoader(std::make_unique<FileSceneLoader>(
        std::filesystem::path("pkg_a_curtains") / "NewSponza_Curtains_glTF.gltf"
    ));
    intelSponzaLoader->AddLoader(std::make_unique<Skybox2DLoader>(
        std::filesystem::path("main_sponza") / "textures" / "kloppenheim_05_4k.hdr", true
    ));

    scenes.emplace("Intel Sponza", std::move(intelSponzaLoader));
}

template<void(load)(SceneBuilder &sceneBuilder)>
void CustomSceneLoader<load>::Load(SceneBuilder &sceneBuilder)
{
    return load(sceneBuilder);
}

void CreateTexturedCubesScene(SceneBuilder &sceneBuilder)
{
    const std::filesystem::path base = Application::GetConfig().AssetFolderPath / "textures";
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
    const std::filesystem::path base = Application::GetConfig().AssetFolderPath / "textures";
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