#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <array>
#include <string>

#include "AssetManager.h"
#include "ExampleScenes.h"

namespace PathTracing::ExampleScenes
{

static void CreateTexturedCubesScene();
static void CreateReuseMeshCubesScene();
static void CreateSponzaScene();
static void CreateChessGameScene();
static void CreateVirtualCity();

void CreateScenes()
{
    // TODO: Only load them on demand
    // CreateTexturedCubesScene();
    CreateReuseMeshCubesScene();
    // CreateSponzaScene();
    // CreateChessGameScene();
    // CreateVirtualCity();
}

static void CreateTexturedCubesScene()
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
            Material {
                materialPath / (material + "_Color.jpg"),
                materialPath / (material + "_NormalGL.jpg"),
                materialPath / (material + "_Roughness.jpg"),
                materialPath / (material + "_Roughness.jpg"),
            }
        );
    }

    std::vector<Shaders::Vertex> vv = {
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

    std::vector<uint32_t> ii = {};
    for (int i = 0; i < 6; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(ii));

    auto vertexIterator = vv.begin();
    auto indexIterator = ii.begin();
    for (uint32_t i = 0; i < 6; i++)
    {
        scene.AddGeometry(std::span(vertexIterator, 4), std::span(indexIterator, 6), true);
        std::advance(vertexIterator, 4);
        std::advance(indexIterator, 6);
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

    const uint32_t cube1inst1 =
        scene.AddModelInstance(cube1, glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f))));
    scene.ModelInstanceNames.Set(cube1inst1, "Cube Instance (ver 1) (inst 1)");
    const uint32_t cube1inst2 =
        scene.AddModelInstance(cube1, glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f))));
    scene.ModelInstanceNames.Set(cube1inst2, "Cube Instance (ver 1) (inst 2)");
    scene.AddModelInstance(
        cube2, glm::transpose(glm::scale(
                   glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, -1.0f, -3.0f)), glm::vec3(2.0f, 1.0f, 0.3f)
               ))
    );

    scene.SetSkybox(Skybox2D { base / "skybox" / "sky_42_2k.png" });

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
            Material {
                materialPath / (material + "_Color.jpg"),
                materialPath / (material + "_NormalGL.jpg"),
                materialPath / (material + "_Roughness.jpg"),
                materialPath / (material + "_Roughness.jpg"),
            }
        );
    }

    std::vector<Shaders::Vertex> vv = {
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

    std::vector<uint32_t> ii = {};
    for (int i = 0; i < 3; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(ii));

    auto vertexIterator = vv.begin();
    auto indexIterator = ii.begin();
    for (uint32_t i = 0; i < 3; i++)
    {
        scene.AddGeometry(std::span(vertexIterator, 4), std::span(indexIterator, 6), true);
        std::advance(vertexIterator, 4);
        std::advance(indexIterator, 6);
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

    const uint32_t cube1inst1 = scene.AddModelInstance(cube, glm::mat4(1.0f));
    scene.ModelInstanceNames.Set(cube1inst1, "Cube Instance");

    const auto skyboxPath = base / "skybox" / "sky_42_cubemap_(roblox)_2k";
    scene.SetSkybox(SkyboxCube {
        skyboxPath / "px.png",
        skyboxPath / "nx.png",
        skyboxPath / "py.png",
        skyboxPath / "ny.png",
        skyboxPath / "pz.png",
        skyboxPath / "nz.png",
    });

    AssetManager::AddScene("Reuse Mesh", std::move(scene));
}

static void CreateSponzaScene()
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

void CreateVirtualCity()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "VC" / "glTF" / "VC.gltf";
    AssetManager::LoadScene("Virtual City", path);
}

}