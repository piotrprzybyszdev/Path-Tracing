#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include <stack>

#include "Core/Core.h"

#include "AssetManager.h"

namespace PathTracing
{

std::unordered_map<std::filesystem::path, Texture> AssetManager::s_Textures = {};
std::unordered_map<std::string, Scene> AssetManager::s_Scenes = {};

void AssetManager::LoadTexture(const std::filesystem::path &path)
{
    Texture texture = {};
    const std::string pathString = path.string();
    stbi_uc *data =
        stbi_load(pathString.c_str(), &texture.Width, &texture.Height, &texture.Channels, STBI_rgb_alpha);

    if (data == nullptr)
        throw error(std::format("Could not load texture {}: {}", pathString, stbi_failure_reason()));

    texture.Data = reinterpret_cast<std::byte *>(data);

    s_Textures[path] = std::move(texture);
}

const Texture &AssetManager::GetTexture(const std::filesystem::path &path)
{
    return s_Textures[path];
}

void AssetManager::ReleaseTexture(const std::filesystem::path &path)
{
    auto it = s_Textures.find(path);
    stbi_image_free(it->second.Data);
    s_Textures.erase(it);
}

static std::optional<std::filesystem::path> ReadTexture(
    std::filesystem::path base, const aiMaterial *material, aiTextureType type
)
{
    const uint32_t cnt = material->GetTextureCount(type);
    if (cnt == 0)
    {
        logger::trace("Texture {} doesn't exist", aiTextureTypeToString(type));
        return std::optional<std::filesystem::path>();
    }

    assert(cnt == 1);
    aiString path;
    aiReturn ret = material->GetTexture(type, 0, &path);
    assert(ret == aiReturn_SUCCESS);
    logger::trace("Adding texture {} at {}", aiTextureTypeToString(type), path.C_Str());
    return base / std::filesystem::path(path.C_Str());
}

void AssetManager::AddScene(const std::string &name, Scene &&scene)
{
    s_Scenes[name] = std::move(scene);
}

static Assimp::Importer s_Importer = Assimp::Importer();

void AssetManager::LoadScene(const std::string &name, const std::filesystem::path &path)
{
    Timer timer("Scene Load");

    const aiScene *scene = nullptr;
    {
        Timer timer("File Import");
        scene = s_Importer.ReadFile(
            path.string().c_str(), aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace
        );

        if (scene == nullptr)
            throw error(s_Importer.GetErrorString());
    }

    assert((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) == false);
    assert(scene->mRootNode != nullptr);

    // TODO: Add support for lights and cameras
    logger::info("Number of meshes in the scene: {}", scene->mNumMeshes);
    logger::info("Number of materials in the scene: {}", scene->mNumMaterials);
    logger::info("Number of lights in the scene: {}", scene->mNumLights);
    logger::info("Number of cameras in the scene: {}", scene->mNumCameras);

    // TODO: Support embedded textures
    assert(scene->HasTextures() == false);

    Scene outScene;

    std::map<uint32_t, uint32_t> materialIndex = {};
    for (int i = 0; i < scene->mNumMaterials; i++)
    {
        Timer timer("Material Copy");

        const aiMaterial *material = scene->mMaterials[i];
        const char *originalName = material->GetName().C_Str();
        const std::string materialName =
            originalName != std::string("") ? originalName : std::format("Unnamed Material at index {}", i); 

        logger::debug("Adding Material: {}", materialName);
        const Material outMaterial = {
            .Color = ReadTexture(path.parent_path(), material, aiTextureType_BASE_COLOR),
            .Normal = ReadTexture(path.parent_path(), material, aiTextureType_NORMALS),
            .Roughness = ReadTexture(path.parent_path(), material, aiTextureType_DIFFUSE_ROUGHNESS),
            .Metalic = ReadTexture(path.parent_path(), material, aiTextureType_METALNESS),
        };

        materialIndex[i] = outScene.AddMaterial(materialName, outMaterial);
    }

    // TODO (when multithreading): Build the vectors and submit them to the scene class once to avoid a copy
    std::vector<Shaders::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::map<uint32_t, uint32_t> meshToGeometry = {};
    for (int i = 0; i < scene->mNumMeshes; i++)
    {
        const aiMesh *mesh = scene->mMeshes[i];

        {
            // Check if this mesh has the same vertices and indices as another one and reference that geometry
            bool found = false;
            for (int j = 0; j < i; j++)
            {
                if (scene->mMeshes[j]->mFaces == mesh->mFaces &&
                    scene->mMeshes[j]->mNumFaces == mesh->mNumFaces)
                {
                    logger::debug("Adding mesh {} geometry as the same as mesh {} geometry", j, i);
                    meshToGeometry[i] = meshToGeometry[j];
                    found = true;
                    break;
                }
            }

            if (found)
                continue;
        }

        const uint32_t vertexCount = mesh->mNumVertices;
        const uint32_t indexCount = mesh->mNumFaces * 3;
        if (vertices.size() < vertexCount)
            vertices.resize(vertexCount);
        if (indices.size() < indexCount)
            indices.resize(indexCount);

        assert(!mesh->HasTextureCoords(0) || mesh->mNumUVComponents[0] == 2);

        for (int j = 0; j < vertexCount; j++)
        {
            vertices[j].Position = TrivialCopy<aiVector3D, glm::vec3>(mesh->mVertices[j]);
            if (mesh->HasTextureCoords(0))
                vertices[j].TexCoords = TrivialCopy<aiVector3D, glm::vec2>(mesh->mTextureCoords[0][j]);
            vertices[j].Normal = TrivialCopy<aiVector3D, glm::vec3>(mesh->mNormals[j]);
            if (mesh->HasTangentsAndBitangents())
            {
                vertices[j].Tangent = TrivialCopy<aiVector3D, glm::vec3>(mesh->mTangents[j]);
                vertices[j].Bitangent = TrivialCopy<aiVector3D, glm::vec3>(mesh->mBitangents[j]);
            }
        }

        for (int j = 0; j < mesh->mNumFaces; j++)
        {
            const aiFace &face = mesh->mFaces[j];
            assert(face.mNumIndices == 3);
            std::ranges::copy(std::span(face.mIndices, 3), indices.begin() + j * 3);
        }

        bool isOpaque;
        {
            // TODO: Handle other opaque flags from input file
            const aiMaterial *material = scene->mMaterials[mesh->mMaterialIndex];
            if (material->GetTextureCount(aiTextureType_BASE_COLOR) > 0)
            {
                aiString colorTexturePath;
                aiReturn ret = material->GetTexture(aiTextureType_BASE_COLOR, 0, &colorTexturePath);
                assert(ret == aiReturn::aiReturn_SUCCESS);
                int channels;
                int result = stbi_info(colorTexturePath.C_Str(), nullptr, nullptr, &channels);
                assert(result == 0);
                isOpaque = channels == 3;
            }
            else
                isOpaque = true;
        }

        logger::debug(
            "Adding geometry (mesh {}) with {} vertices and {} indices", mesh->mName.C_Str(), vertexCount,
            indexCount
        );
        meshToGeometry[i] = outScene.AddGeometry(
            std::span(vertices.data(), vertexCount), std::span(indices.data(), indexCount), isOpaque
        );
    }

    const auto toTransformMatrix = TrivialCopy<aiMatrix4x4, glm::mat4>;

    {
        // Meshes of the top level node are considered to be the models
        std::vector<MeshInfo> meshInfos;
        for (int j = 0; j < scene->mRootNode->mNumMeshes; j++)
        {
            meshInfos.emplace_back(
                meshToGeometry[scene->mRootNode->mMeshes[j]],
                materialIndex[scene->mMeshes[scene->mRootNode->mMeshes[j]]->mMaterialIndex], glm::mat4(1.0f)
            );
            logger::info("{}, mesh count: {}", scene->mRootNode->mName.C_Str(), scene->mRootNode->mNumMeshes);
        }

        if (!meshInfos.empty())
        {
            const uint32_t modelIndex = outScene.AddModel(meshInfos);
            outScene.AddModelInstance(modelIndex, toTransformMatrix(scene->mRootNode->mTransformation));
        }
    }

    // We consider top level nodes to be the models
    // Meshes of a model are all of the meshes of all nodes of their children
    for (int i = 0; i < scene->mRootNode->mNumChildren; i++)
    {
        const aiNode *startNode = scene->mRootNode->mChildren[i];

        std::vector<MeshInfo> meshInfos;
        std::stack<std::tuple<const aiNode *, aiMatrix4x4, int>> stack;

        stack.push({ startNode, aiMatrix4x4(), 0 });

        while (!stack.empty())
        {
            const auto [node, parentTransform, depth] = stack.top();
            stack.pop();

            logger::info(
                "{}{}, mesh count: {}", std::string(depth * 4, ' '), node->mName.C_Str(), node->mNumMeshes
            );

            const aiMatrix4x4 transform = parentTransform * node->mTransformation;
            for (int j = 0; j < node->mNumMeshes; j++)
            {
                meshInfos.emplace_back(
                    meshToGeometry[node->mMeshes[j]],
                    materialIndex[scene->mMeshes[node->mMeshes[j]]->mMaterialIndex],
                    toTransformMatrix(transform)
                );
            }

            for (int j = 0; j < node->mNumChildren; j++)
                stack.push({ node->mChildren[j], transform, depth + 1 });
        }

        if (!meshInfos.empty())
        {
            const uint32_t modelIndex = outScene.AddModel(meshInfos);
            outScene.AddModelInstance(modelIndex, toTransformMatrix(scene->mRootNode->mTransformation));
        }
    }

    s_Scenes[name] = std::move(outScene);
}

const Scene &AssetManager::GetScene(const std::string &name)
{
    return s_Scenes[name];
}

void AssetManager::ReleaseScene(const std::string &name)
{
    auto it = s_Scenes.find(name);
    s_Scenes.erase(it);
}

}