#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include <stack>

#include "Core/Core.h"

#include "AssetManager.h"

namespace PathTracing
{

std::unordered_map<std::string, Scene> AssetManager::s_Scenes = {};

std::byte *AssetManager::LoadTextureData(const TextureInfo &info)
{
    const std::string pathString = info.Path.string();

    int x, y, channels;
    stbi_uc *data = stbi_load(pathString.c_str(), &x, &y, &channels, STBI_rgb_alpha);

    assert(x == info.Width && y == info.Height && channels == info.Channels);

    if (data == nullptr)
        throw error(std::format("Could not load texture {}: {}", pathString, stbi_failure_reason()));

    return reinterpret_cast<std::byte *>(data);
}

void AssetManager::ReleaseTextureData(std::byte *data)
{
    stbi_image_free(data);
}

void AssetManager::AddScene(const std::string &name, Scene &&scene)
{
    s_Scenes[name] = std::move(scene);
}

TextureInfo AssetManager::GetTextureInfo(const std::filesystem::path path, TextureType type)
{
    std::string pathString = path.string();
    int x, y, channels;
    int ret = stbi_info(pathString.c_str(), &x, &y, &channels);

    if (ret == 0)
        throw error(std::format("Could not load texture {}: {}", pathString, stbi_failure_reason()));

    return TextureInfo(type, channels, x, y, path);
}

namespace
{

Assimp::Importer s_Importer = Assimp::Importer();

TextureType ToTextureType(aiTextureType type)
{
    switch (type)
    {
    case aiTextureType_BASE_COLOR:
        return TextureType::Color;
    case aiTextureType_NORMALS:
        return TextureType::Normal;
    case aiTextureType_DIFFUSE_ROUGHNESS:
        return TextureType::Roughness;
    case aiTextureType_METALNESS:
        return TextureType::Metalic;
    default:
        throw error(std::format("Unsupported Texture type {}", static_cast<uint8_t>(type)));
    }
}

uint32_t GetDefaultTextureIndex(TextureType type)
{
    switch (type)
    {
    case TextureType::Color:
        return Shaders::DefaultColorTextureIndex;
    case TextureType::Normal:
        return Shaders::DefaultNormalTextureIndex;
    case TextureType::Roughness:
        return Shaders::DefaultRoughnessTextureIndex;
    case TextureType::Metalic:
        return Shaders::DefaultMetalicTextureIndex;
    default:
        throw error(std::format("Unsupported Texture type {}", static_cast<uint8_t>(type)));
    }
}

uint32_t AddTexture(
    Scene &outScene, const std::filesystem::path &base, const aiMaterial *material, aiTextureType type
)
{
    const uint32_t cnt = material->GetTextureCount(type);
    const TextureType textureType = ToTextureType(type);

    if (cnt == 0)
    {
        logger::trace("Texture {} doesn't exist", aiTextureTypeToString(type));
        return GetDefaultTextureIndex(textureType);
    }

    aiString path;
    {
        assert(cnt == 1);
        aiReturn ret = material->GetTexture(type, 0, &path);
        assert(ret == aiReturn_SUCCESS);
    }

    logger::trace("Adding texture {} at {}", aiTextureTypeToString(type), path.C_Str());

    std::filesystem::path texturePath = base.string() / std::filesystem::path(path.C_Str());

    return outScene.AddTexture(AssetManager::GetTextureInfo(texturePath, textureType));
}

std::vector<uint32_t> LoadMaterials(const std::filesystem::path &path, Scene &outScene, const aiScene *scene)
{
    std::vector<uint32_t> materialIndexMap(scene->mNumMaterials);
    for (int i = 0; i < scene->mNumMaterials; i++)
    {
        const aiMaterial *material = scene->mMaterials[i];
        const aiString originalName = material->GetName();
        const std::string materialName =
            originalName.length != 0 ? originalName.C_Str() : std::format("Unnamed Material at index {}", i);

        const Shaders::Material outMaterial = {
            .ColorIdx = AddTexture(outScene, path.parent_path(), material, aiTextureType_BASE_COLOR),
            .NormalIdx = AddTexture(outScene, path.parent_path(), material, aiTextureType_NORMALS),
            .RoughnessIdx =
                AddTexture(outScene, path.parent_path(), material, aiTextureType_DIFFUSE_ROUGHNESS),
            .MetalicIdx = AddTexture(outScene, path.parent_path(), material, aiTextureType_METALNESS),
        };

        materialIndexMap[i] = outScene.AddMaterial(materialName, outMaterial);
        logger::debug("Added Material: {}", materialName);
    }

    return materialIndexMap;
}

bool CheckOpaque(const aiMaterial *material)
{
    // TODO: Handle other opaque flags from input file
    if (material->GetTextureCount(aiTextureType_BASE_COLOR) == 0)
        return true;

    aiString colorTexturePath;
    aiReturn ret = material->GetTexture(aiTextureType_BASE_COLOR, 0, &colorTexturePath);
    assert(ret == aiReturn::aiReturn_SUCCESS);
    int channels;
    int result = stbi_info(colorTexturePath.C_Str(), nullptr, nullptr, &channels);
    assert(result == 0);
    return channels == 3;
}

// Some meshes might differ only in material, but have the same geometry
uint32_t FindSameGeometry(std::span<aiMesh *const> haystack, const aiMesh *needle)
{
    // TODO: Should check that both materials are opaque

    for (uint32_t i = 0; i < haystack.size(); i++)
        if (haystack[i]->mFaces == needle->mFaces && haystack[i]->mNumFaces == needle->mNumFaces)
            return i;

    return haystack.size();
}

std::vector<uint32_t> LoadMeshes(Scene &outScene, const aiScene *scene)
{
    std::vector<Shaders::Vertex> vertices;
    std::vector<uint32_t> indices;

    {
        uint32_t vertexCount = 0, indexCount = 0;
        for (int i = 0; i < scene->mNumMeshes; i++)
        {
            vertexCount += scene->mMeshes[i]->mNumVertices;
            indexCount += scene->mMeshes[i]->mNumFaces * 3;
        }
        vertices.resize(vertexCount);
        indices.resize(indexCount);
    }

    std::vector<uint32_t> meshToGeometry(scene->mNumMeshes);
    uint32_t vertexOffset = 0, indexOffset = 0;
    for (uint32_t i = 0; i < scene->mNumMeshes; i++)
    {
        const aiMesh *mesh = scene->mMeshes[i];

        const uint32_t otherGeometryIndex = FindSameGeometry(std::span(scene->mMeshes, i), mesh);
        if (otherGeometryIndex != i)
        {
            const aiMesh *other = scene->mMeshes[otherGeometryIndex];
            logger::debug(
                "Adding geometry of mesh {} (idx: {}) as the same as geometry of mesh {} (idx: {})",
                mesh->mName.C_Str(), i, other->mName.C_Str(), otherGeometryIndex
            );
            continue;
        }

        const uint32_t vertexCount = mesh->mNumVertices;
        const uint32_t indexCount = mesh->mNumFaces * 3;

        assert(!mesh->HasTextureCoords(0) || mesh->mNumUVComponents[0] == 2);

        for (int j = 0; j < vertexCount; j++)
        {
            const uint32_t idx = vertexOffset + j;
            vertices[idx].Position = TrivialCopy<aiVector3D, glm::vec3>(mesh->mVertices[j]);
            if (mesh->HasTextureCoords(0))
                vertices[idx].TexCoords = TrivialCopy<aiVector3D, glm::vec2>(mesh->mTextureCoords[0][j]);
            vertices[idx].Normal = TrivialCopy<aiVector3D, glm::vec3>(mesh->mNormals[j]);
            if (mesh->HasTangentsAndBitangents())
            {
                vertices[idx].Tangent = TrivialCopy<aiVector3D, glm::vec3>(mesh->mTangents[j]);
                vertices[idx].Bitangent = TrivialCopy<aiVector3D, glm::vec3>(mesh->mBitangents[j]);
            }
        }

        for (int j = 0; j < mesh->mNumFaces; j++)
        {
            const uint32_t idx = indexOffset + j * 3;
            const aiFace &face = mesh->mFaces[j];
            assert(face.mNumIndices == 3);
            std::ranges::copy(std::span(face.mIndices, 3), indices.begin() + idx);
        }

        bool isOpaque = CheckOpaque(scene->mMaterials[mesh->mMaterialIndex]);

        meshToGeometry[i] =
            outScene.AddGeometry({ vertexOffset, vertexCount, indexOffset, indexCount, isOpaque });
        vertexOffset += vertexCount;
        indexOffset += indexCount;

        logger::debug(
            "Adding geometry (mesh {}) with {} vertices and {} indices", mesh->mName.C_Str(), vertexCount,
            indexCount
        );
    }

    outScene.SetVertices(std::move(vertices));
    outScene.SetIndices(std::move(indices));

    return meshToGeometry;
}

}

void AssetManager::LoadScene(const std::string &name, const std::filesystem::path &path)
{
    Timer timer("Scene Load");

    const aiScene *scene = nullptr;
    {
        unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace;
#ifdef NDEBUG
        flags |= aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality | aiProcess_OptimizeMeshes;
#endif

        Timer timer("File Import");
        scene = s_Importer.ReadFile(path.string().c_str(), flags);

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

    std::vector<uint32_t> materialIndexMap = LoadMaterials(path, outScene, scene);
    std::vector<uint32_t> meshToGeometry = LoadMeshes(outScene, scene);

    const auto toTransformMatrix = TrivialCopy<aiMatrix4x4, glm::mat4>;

    {
        // Meshes of the top level node are considered to be the models
        std::vector<MeshInfo> meshInfos;
        for (int j = 0; j < scene->mRootNode->mNumMeshes; j++)
        {
            meshInfos.emplace_back(
                meshToGeometry[scene->mRootNode->mMeshes[j]],
                materialIndexMap[scene->mMeshes[scene->mRootNode->mMeshes[j]]->mMaterialIndex],
                glm::mat4(1.0f)
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

        stack.emplace(startNode, aiMatrix4x4(), 0);

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
                    materialIndexMap[scene->mMeshes[node->mMeshes[j]]->mMaterialIndex],
                    toTransformMatrix(transform)
                );
            }

            for (int j = 0; j < node->mNumChildren; j++)
                stack.emplace(node->mChildren[j], transform, depth + 1);
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
    auto it = s_Scenes.find(name);
    assert(it != s_Scenes.end());
    return it->second;
}

void AssetManager::ReleaseScene(const std::string &name)
{
    auto it = s_Scenes.find(name);
    s_Scenes.erase(it);
}

}
