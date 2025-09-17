#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <array>
#include <string>

#include "AssetManager.h"
#include "ExampleScenes.h"

namespace PathTracing::ExampleScenes
{

void CreateTexturedCubesScene();
void CreateReuseMeshCubesScene();
void CreateSponzaScene();
void CreateChessGameScene();
void CreateVirtualCityScene();
void CreateCesiumManScene();
void CreateCesiumMilkTruckScene();
void CreateBrainStemScene();
void CreateBoxAnimatedScene();

void CreateScenes()
{
    // TODO: Only load them on demand
    CreateTexturedCubesScene();
    CreateReuseMeshCubesScene();
    // CreateSponzaScene();
    // CreateChessGameScene();
    CreateVirtualCityScene();
    // CreateCesiumManScene();
    // CreateCesiumMilkTruckScene();
    // CreateBrainStemScene();
    CreateBoxAnimatedScene();
}

void CreateTexturedCubesScene()
{
    Scene scene;

    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "textures";
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
        scene.AddMaterial(
            assetNames[i],
            Shaders::Material {
                scene.AddTexture(
                    AssetManager::GetTextureInfo(materialPath / (material + "_Color.jpg"), TextureType::Color)
                ),
                scene.AddTexture(AssetManager::GetTextureInfo(
                    materialPath / (material + "_NormalGL.jpg"), TextureType::Normal
                )),
                scene.AddTexture(AssetManager::GetTextureInfo(
                    materialPath / (material + "_Roughness.jpg"), TextureType::Roughness
                )),
                scene.AddTexture(AssetManager::GetTextureInfo(
                    materialPath / (material + "_Roughness.jpg"), TextureType::Metalic
                )),
            }
        );
    }

    std::vector<Shaders::Vertex> vertices = {
        { { -1, -1, 1 }, { 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1, -1, 1 }, { 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1, 1, 1 }, { 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { -1, 1, 1 }, { 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },

        { { 1, -1, -1 }, { 1, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { -1, -1, -1 }, { 0, 1 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { -1, 1, -1 }, { 0, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },
        { { 1, 1, -1 }, { 1, 0 }, { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } },

        { { -1, -1, -1 }, { 1, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, -1, 1 }, { 0, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, 1, 1 }, { 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, 1, -1 }, { 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },

        { { 1, -1, 1 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { 1, -1, -1 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { 1, 1, -1 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },
        { { 1, 1, 1 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },

        { { -1, 1, 1 }, { 1, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1, 1, 1 }, { 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1, 1, -1 }, { 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { -1, 1, -1 }, { 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },

        { { -1, -1, -1 }, { 1, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { 1, -1, -1 }, { 0, 1 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { 1, -1, 1 }, { 0, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
        { { -1, -1, 1 }, { 1, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
    };

    std::vector<uint32_t> indices = {};
    for (int i = 0; i < 6; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    scene.SetVertices(std::move(vertices));
    scene.SetIndices(std::move(indices));

    uint32_t vertexOffset = 0, indexOffset = 0;
    for (uint32_t i = 0; i < 6; i++)
    {
        scene.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
        vertexOffset += 4;
        indexOffset += 6;
    }

    std::array<MeshInfo, 6> m1 = { {
        { 0, 0, glm::mat4(1.0f) },
        { 1, 0, glm::mat4(1.0f) },
        { 2, 1, glm::mat4(1.0f) },
        { 3, 1, glm::mat4(1.0f) },
        { 4, 2, glm::mat4(1.0f) },
        { 5, 2, glm::mat4(1.0f) },
    } };

    std::array<MeshInfo, 6> m2 = { {
        { 0, 0, glm::mat4(1.0f) },
        { 1, 0, glm::mat4(1.0f) },
        { 2, 0, glm::mat4(1.0f) },
        { 3, 0, glm::mat4(1.0f) },
        { 4, 0, glm::mat4(1.0f) },
        { 5, 0, glm::mat4(1.0f) },
    } };

    const uint32_t cube1 = scene.AddModel(m1);
    scene.ModelNames.Set(cube1, "Cube different materials (ver 1)");
    scene.MeshNames.Set({ cube1, 5 }, "Bottom Square");
    scene.MeshNames.Set({ cube1, 6 }, "Top Square");

    const uint32_t cube2 = scene.AddModel(m2);
    scene.ModelNames.Set(cube2, "Cube one material (ver 2)");

    const glm::mat4 cube1inst1transform = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f)));
    const glm::mat4 cube1inst2transform = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f)));
    const glm::mat4 cube2transform = glm::transpose(glm::scale(
        glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, -1.0f, -3.0f)), glm::vec3(2.0f, 1.0f, 0.3f)
    ));

    const uint32_t rootNode = scene.AddSceneNode({ 0u, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1inst1node = scene.AddSceneNode({ rootNode, cube1inst1transform, glm::mat4(1.0f) });
    const uint32_t cube1inst2node = scene.AddSceneNode({ rootNode, cube1inst2transform, glm::mat4(1.0f) });
    const uint32_t cube2node = scene.AddSceneNode({ rootNode, cube2transform, glm::mat4(1.0f) });

    const uint32_t cube1inst1 = scene.AddModelInstance({ cube1, cube1inst1node });
    scene.ModelInstanceNames.Set(cube1inst1, "Cube Instance (ver 1) (inst 1)");
    const uint32_t cube1inst2 = scene.AddModelInstance({ cube1, cube1inst2node });
    scene.ModelInstanceNames.Set(cube1inst2, "Cube Instance (ver 1) (inst 2)");
    const uint32_t cube2inst = scene.AddModelInstance({ cube2, cube2node });
    scene.ModelInstanceNames.Set(cube2inst, "Cube Instance (ver 2) (inst 1)");

    scene.SetSkybox(
        Skybox2D(AssetManager::GetTextureInfo(base / "skybox" / "sky_42_2k.png", TextureType::Skybox))
    );

    AssetManager::AddScene("Textured Cubes", std::move(scene));
}

void CreateReuseMeshCubesScene()
{
    Scene scene;

    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "textures";
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
        scene.AddMaterial(
            assetNames[i],
            Shaders::Material {
                scene.AddTexture(
                    AssetManager::GetTextureInfo(materialPath / (material + "_Color.jpg"), TextureType::Color)
                ),
                scene.AddTexture(AssetManager::GetTextureInfo(
                    materialPath / (material + "_NormalGL.jpg"), TextureType::Normal
                )),
                scene.AddTexture(AssetManager::GetTextureInfo(
                    materialPath / (material + "_Roughness.jpg"), TextureType::Roughness
                )),
                scene.AddTexture(AssetManager::GetTextureInfo(
                    materialPath / (material + "_Roughness.jpg"), TextureType::Metalic
                )),
            }
        );
    }

    std::vector<Shaders::Vertex> vertices = {
        { { -1, -1, 1 }, { 1, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1, -1, 1 }, { 0, 1 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { 1, 1, 1 }, { 0, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        { { -1, 1, 1 }, { 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },

        { { -1, -1, -1 }, { 1, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, -1, 1 }, { 0, 1 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, 1, 1 }, { 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
        { { -1, 1, -1 }, { 1, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },

        { { -1, 1, 1 }, { 1, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1, 1, 1 }, { 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { 1, 1, -1 }, { 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
        { { -1, 1, -1 }, { 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
    };

    std::vector<uint32_t> indices = {};
    for (int i = 0; i < 3; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    scene.SetVertices(std::move(vertices));
    scene.SetIndices(std::move(indices));

    uint32_t vertexOffset = 0, indexOffset = 0;
    for (uint32_t i = 0; i < 3; i++)
    {
        scene.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
        vertexOffset += 4;
        indexOffset += 6;
    }

    std::array<MeshInfo, 6> m = { {
        { 0, 1, glm::mat4(1.0f) },
        { 0, 1,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f))) },
        { 1, 1, glm::mat4(1.0f) },
        { 1, 2,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f))) },
        { 2, 2, glm::mat4(1.0f) },
        { 2, 2,
          glm::transpose(glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f))) },
    } };

    const uint32_t cube = scene.AddModel(m);
    scene.ModelNames.Set(cube, "Cube Model");
    scene.MeshNames.Set({ cube, 0 }, "z-axis plane (+) (mat 1)");
    scene.MeshNames.Set({ cube, 1 }, "z-axis plane (-) (mat 1)");
    scene.MeshNames.Set({ cube, 2 }, "x-axis plane (-) (mat 1)");
    scene.MeshNames.Set({ cube, 3 }, "x-axis plane (+) (mat 2)");
    scene.MeshNames.Set({ cube, 4 }, "y-axis plane (+) (mat 2)");
    scene.MeshNames.Set({ cube, 5 }, "y-axis plane (-) (mat 2)");

    const uint32_t rootNode = scene.AddSceneNode({ 0u, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1node = scene.AddSceneNode({ rootNode, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1inst = scene.AddModelInstance({ cube, cube1node });
    scene.ModelInstanceNames.Set(cube1inst, "Cube Instance");

    const auto skyboxPath = base / "skybox" / "sky_42_cubemap_(roblox)_2k";
    scene.SetSkybox(SkyboxCube(
        AssetManager::GetTextureInfo(skyboxPath / "px.png", TextureType::Skybox),
        AssetManager::GetTextureInfo(skyboxPath / "nx.png", TextureType::Skybox),
        AssetManager::GetTextureInfo(skyboxPath / "py.png", TextureType::Skybox),
        AssetManager::GetTextureInfo(skyboxPath / "ny.png", TextureType::Skybox),
        AssetManager::GetTextureInfo(skyboxPath / "pz.png", TextureType::Skybox),
        AssetManager::GetTextureInfo(skyboxPath / "nz.png", TextureType::Skybox)
    ));

    AssetManager::AddScene("Reuse Mesh", std::move(scene));
}

void CreateSponzaScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "Sponza" / "glTF" / "Sponza.gltf";
    AssetManager::LoadScene("Sponza", path);
}

void CreateChessGameScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" /
                                       "ABeautifulGame" / "glTF" / "ABeautifulGame.gltf";
    AssetManager::LoadScene("Chess Game", path);
}

void CreateVirtualCityScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "VC" / "glTF" / "VC.gltf";
    AssetManager::LoadScene("Virtual City", path);
}

void CreateCesiumManScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "CesiumMan" / "glTF" / "CesiumMan.gltf";
    AssetManager::LoadScene("Cesium Man", path);
}

void CreateCesiumMilkTruckScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "CesiumMilkTruck" / "glTF" / "CesiumMilkTruck.gltf";
    AssetManager::LoadScene("Cesium Milk Truck", path);
}

void CreateBrainStemScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" /
                                       "BrainStem" / "glTF" / "BrainStem.gltf";
    AssetManager::LoadScene("Brain Stem", path);
}

void CreateBoxAnimatedScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" /
                                       "BoxAnimated" / "glTF" / "BoxAnimated.gltf";
    AssetManager::LoadScene("Box Animated", path);
}

}