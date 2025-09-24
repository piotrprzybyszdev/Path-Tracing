#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <algorithm>
#include <array>
#include <string>

#include "AssetImporter.h"
#include "ExampleScenes.h"

namespace PathTracing::ExampleScenes
{

std::shared_ptr<Scene> CreateTexturedCubesScene()
{
    SceneBuilder sceneBuilder;

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
        sceneBuilder.AddMaterial(
            assetNames[i],
            Shaders::Material {
                sceneBuilder.AddTexture(
                    AssetImporter::GetTextureInfo(materialPath / (material + "_Color.jpg"), TextureType::Color)
                ),
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

    std::vector<Shaders::Vertex> vertices = {
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

    std::vector<uint32_t> indices = {};
    for (int i = 0; i < 6; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    sceneBuilder.SetVertices(std::move(vertices));
    sceneBuilder.SetIndices(std::move(indices));

    uint32_t vertexOffset = 0, indexOffset = 0;
    for (uint32_t i = 0; i < 6; i++)
    {
        sceneBuilder.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
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

    const uint32_t cube1 = sceneBuilder.AddModel(m1);
    const uint32_t cube2 = sceneBuilder.AddModel(m2);

    const glm::mat4 cube1inst1transform = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(1.0f)));
    const glm::mat4 cube1inst2transform = glm::transpose(glm::translate(glm::mat4(1.0f), glm::vec3(-1.0f)));
    const glm::mat4 cube2transform = glm::transpose(glm::scale(
        glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, -1.0f, -3.0f)), glm::vec3(2.0f, 1.0f, 0.3f)
    ));

    const uint32_t rootNode = sceneBuilder.AddSceneNode({ 0u, glm::mat4(1.0f), glm::mat4(1.0f) });
    const uint32_t cube1inst1node = sceneBuilder.AddSceneNode({ rootNode, cube1inst1transform, glm::mat4(1.0f) });
    const uint32_t cube1inst2node = sceneBuilder.AddSceneNode({ rootNode, cube1inst2transform, glm::mat4(1.0f) });
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

    return sceneBuilder.CreateSceneShared("Textured Cubes");
}

std::shared_ptr<Scene> CreateReuseMeshCubesScene()
{
    SceneBuilder sceneBuilder;

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
        sceneBuilder.AddMaterial(
            assetNames[i],
            Shaders::Material {
                sceneBuilder.AddTexture(
                    AssetImporter::GetTextureInfo(materialPath / (material + "_Color.jpg"), TextureType::Color)
                ),
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

    std::vector<Shaders::Vertex> vertices = {
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

    std::vector<uint32_t> indices = {};
    for (int i = 0; i < 3; i++)
        std::ranges::copy(std::vector<uint32_t> { 0, 1, 2, 2, 3, 0 }, std::back_inserter(indices));

    sceneBuilder.SetVertices(std::move(vertices));
    sceneBuilder.SetIndices(std::move(indices));

    uint32_t vertexOffset = 0, indexOffset = 0;
    for (uint32_t i = 0; i < 3; i++)
    {
        sceneBuilder.AddGeometry({ vertexOffset, 4, indexOffset, 6, true });
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

    return sceneBuilder.CreateSceneShared("Reuse Mesh");
}

std::shared_ptr<Scene> CreateSponzaScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "Sponza" / "glTF" / "Sponza.gltf";
    return AssetImporter::LoadScene("Sponza", path);
}

std::shared_ptr<Scene> CreateChessGameScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" /
                                       "ABeautifulGame" / "glTF" / "ABeautifulGame.gltf";
    return AssetImporter::LoadScene("Chess Game", path);
}

std::shared_ptr<Scene> CreateVirtualCityScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "VC" / "glTF" / "VC.gltf";
    return AssetImporter::LoadScene("Virtual City", path);
}

std::shared_ptr<Scene> CreateCesiumManScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "CesiumMan" / "glTF" / "CesiumMan.gltf";
    return AssetImporter::LoadScene("Cesium Man", path);
}

std::shared_ptr<Scene> CreateCesiumMilkTruckScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path =
        base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" / "CesiumMilkTruck" / "glTF" / "CesiumMilkTruck.gltf";
    return AssetImporter::LoadScene("Cesium Milk Truck", path);
}

std::shared_ptr<Scene> CreateBrainStemScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" /
                                       "BrainStem" / "glTF" / "BrainStem.gltf";
    return AssetImporter::LoadScene("Brain Stem", path);
}

std::shared_ptr<Scene> CreateBoxAnimatedScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" /
                                       "BoxAnimated" / "glTF" / "BoxAnimated.gltf";
    return AssetImporter::LoadScene("Box Animated", path);
}

std::shared_ptr<Scene> CreateLampLightScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" /
                                       "LightsPunctualLamp" / "glTF" / "LightsPunctualLamp.gltf";
    return AssetImporter::LoadScene("Lamp", path);
}

std::shared_ptr<Scene> CreateBigSponzaScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "main_sponza" / "NewSponza_Main_glTF_003.gltf";
    return AssetImporter::LoadScene("Big Sponza", path);
}

std::shared_ptr<Scene> CreateRiggedSimpleScene()
{
    const std::filesystem::path base = std::filesystem::current_path().parent_path() / "assets" / "scenes";
    const std::filesystem::path path = base / "KhronosScenes" / "glTF-Sample-Models-main" / "2.0" /
                                       "RiggedSimple" / "glTF" / "RiggedSimple.gltf";
    return AssetImporter::LoadScene("Rigged Simple", path);
}

}