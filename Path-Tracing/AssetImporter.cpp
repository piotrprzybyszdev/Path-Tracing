#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#include <stack>

#include "Core/Core.h"

#include "AssetImporter.h"

namespace PathTracing
{

std::byte *AssetImporter::LoadTextureData(const TextureInfo &info)
{
    const std::string pathString = info.Path.string();

    int x, y, channels;
    stbi_uc *data = stbi_load(pathString.c_str(), &x, &y, &channels, STBI_rgb_alpha);

    assert(x == info.Width && y == info.Height && channels == info.Channels);

    if (data == nullptr)
        throw error(std::format("Could not load texture {}: {}", pathString, stbi_failure_reason()));

    return reinterpret_cast<std::byte *>(data);
}

void AssetImporter::ReleaseTextureData(std::byte *data)
{
    stbi_image_free(data);
}

TextureInfo AssetImporter::GetTextureInfo(const std::filesystem::path path, TextureType type)
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
    SceneBuilder &sceneBuilder, const std::filesystem::path &base, const aiMaterial *material,
    aiTextureType type
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

    return sceneBuilder.AddTexture(AssetImporter::GetTextureInfo(texturePath, textureType));
}

std::vector<uint32_t> LoadMaterials(
    const std::filesystem::path &path, SceneBuilder &sceneBuilder, const aiScene *scene
)
{
    std::vector<uint32_t> materialIndexMap(scene->mNumMaterials);
    for (int i = 0; i < scene->mNumMaterials; i++)
    {
        const aiMaterial *material = scene->mMaterials[i];
        const aiString originalName = material->GetName();
        const std::string materialName =
            originalName.length != 0 ? originalName.C_Str() : std::format("Unnamed Material at index {}", i);

        const Shaders::Material outMaterial = {
            .ColorIdx = AddTexture(sceneBuilder, path.parent_path(), material, aiTextureType_BASE_COLOR),
            .NormalIdx = AddTexture(sceneBuilder, path.parent_path(), material, aiTextureType_NORMALS),
            .RoughnessIdx =
                AddTexture(sceneBuilder, path.parent_path(), material, aiTextureType_DIFFUSE_ROUGHNESS),
            .MetalicIdx = AddTexture(sceneBuilder, path.parent_path(), material, aiTextureType_METALNESS),
        };

        materialIndexMap[i] = sceneBuilder.AddMaterial(materialName, outMaterial);
        logger::debug("Added Material: {}", materialName);
    }

    return materialIndexMap;
}

bool CheckOpaque(const std::filesystem::path &path, const aiMaterial *material)
{
    // TODO: Handle other opaque flags from input file
    if (material->GetTextureCount(aiTextureType_BASE_COLOR) == 0)
        return true;

    aiString colorTexturePath;
    aiReturn ret = material->GetTexture(aiTextureType_BASE_COLOR, 0, &colorTexturePath);
    assert(ret == aiReturn::aiReturn_SUCCESS);
    std::string fullPath = (path.parent_path() / std::filesystem::path(colorTexturePath.C_Str())).string();
    int channels;
    int result = stbi_info(fullPath.c_str(), nullptr, nullptr, &channels);
    assert(result == 1);
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

std::vector<uint32_t> LoadMeshes(
    SceneBuilder &sceneBuilder, const std::filesystem::path &path, const aiScene *scene
)
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

        bool isOpaque = CheckOpaque(path, scene->mMaterials[mesh->mMaterialIndex]);

        meshToGeometry[i] =
            sceneBuilder.AddGeometry({ vertexOffset, vertexCount, indexOffset, indexCount, isOpaque });
        vertexOffset += vertexCount;
        indexOffset += indexCount;

        logger::debug(
            "Adding geometry (mesh {}) ({}) with {} vertices and {} indices", mesh->mName.C_Str(),
            isOpaque ? "Opaque" : "Not opaque", vertexCount, indexCount
        );
    }

    sceneBuilder.SetVertices(std::move(vertices));
    sceneBuilder.SetIndices(std::move(indices));

    return meshToGeometry;
}

std::unordered_set<const aiNode *> FindDynamicNodes(const aiScene *scene)
{
    std::unordered_set<const aiNode *> dynamicNodes;

    for (int i = 0; i < scene->mNumAnimations; i++)
    {
        const aiAnimation *animation = scene->mAnimations[i];
        if (animation->mNumMorphMeshChannels > 0)
            logger::warn(
                "Animation {} contains morph animation channels which are not supported",
                animation->mName.C_Str()
            );

        if (animation->mNumMeshChannels > 0)
            logger::warn(
                "Animation {} contains mesh animation channels which are not supported",
                animation->mName.C_Str()
            );

        {
            const aiString originalName = animation->mName;
            const std::string animationName = originalName.length != 0
                                                  ? originalName.C_Str()
                                                  : std::format("Unnamed Animation at index {}", i);
            logger::info("{} ({:.1f}s)", animationName, animation->mDuration / animation->mTicksPerSecond);
        }

        for (int j = 0; j < animation->mNumChannels; j++)
        {
            const aiNodeAnim *animNode = animation->mChannels[j];
            logger::info("    animates node named: {}", animNode->mNodeName.C_Str());

            const aiNode *node = scene->mRootNode->FindNode(animNode->mNodeName);
            if (node == nullptr)
                logger::warn(
                    "Scene node corresponding to the animation node {} not found", animNode->mNodeName.C_Str()
                );
            else
                dynamicNodes.insert(node);
        }
    }

    return dynamicNodes;
}

std::unordered_map<const aiNode *, uint32_t> LoadSceneHierarchy(
    SceneBuilder &sceneBuilder, const aiScene *scene, std::span<const uint32_t> meshToGeometry,
    std::span<const uint32_t> materialIndexMap, const std::unordered_set<const aiNode *> &dynamicNodes
)
{
    std::unordered_map<const aiNode *, uint32_t> sceneNodeToIndex;

    const auto toTransformMatrix = TrivialCopy<aiMatrix4x4, glm::mat4>;
    std::vector<std::vector<MeshInfo>> modelToMeshInfos(1 + dynamicNodes.size());
    std::vector<uint32_t> sceneNodeIndices(1 + dynamicNodes.size());

    std::stack<std::tuple<const aiNode *, glm::mat4, uint32_t, uint32_t, int>> stack;
    stack.emplace(scene->mRootNode, glm::mat4(1.0f), 0u, -1u, 0);

    uint32_t nextModelIndex = 0;

    while (!stack.empty())
    {
        auto [node, totalTransform, modelIndex, parentNodeIndex, depth] = stack.top();
        stack.pop();

        logger::info(
            "{}{}, mesh count: {}", std::string(depth * 4, ' '), node->mName.C_Str(), node->mNumMeshes
        );

        glm::mat4 nodeTransform = toTransformMatrix(node->mTransformation);

        const uint32_t sceneNodeIndex = sceneBuilder.AddSceneNode({
            .Parent = parentNodeIndex,
            .Transform = nodeTransform,
            .CurrentTransform = glm::mat4(1.0f),
        });

        sceneNodeToIndex.emplace(node, sceneNodeIndex);
        totalTransform = nodeTransform * totalTransform;

        if (dynamicNodes.contains(node) || node == scene->mRootNode)
        {
            modelIndex = nextModelIndex++;
            sceneNodeIndices[modelIndex] = sceneNodeIndex;
            totalTransform = glm::mat4(1.0f);
        }

        for (int i = 0; i < node->mNumMeshes; i++)
            modelToMeshInfos[modelIndex].emplace_back(
                meshToGeometry[node->mMeshes[i]],
                materialIndexMap[scene->mMeshes[node->mMeshes[i]]->mMaterialIndex], totalTransform
            );

        for (int i = 0; i < node->mNumChildren; i++)
            stack.emplace(node->mChildren[i], totalTransform, modelIndex, sceneNodeIndex, depth + 1);
    }

    // TODO: Combine models into one if their meshInfos are the same

    for (int i = 0; i < modelToMeshInfos.size(); i++)
        if (!modelToMeshInfos[i].empty())
            sceneBuilder.AddModelInstance(sceneBuilder.AddModel(modelToMeshInfos[i]), sceneNodeIndices[i]);

    return sceneNodeToIndex;
}
 
void LoadAnimations(
    SceneBuilder &sceneBuilder, const aiScene *scene,
    const std::unordered_map<const aiNode *, uint32_t> &sceneNodeIndices
)
{
    for (int i = 0; i < scene->mNumAnimations; i++)
    {
        const aiAnimation *animation = scene->mAnimations[i];
        Animation outAnimation = {
            .TickPerSecond = static_cast<float>(animation->mTicksPerSecond),
            .TickDuration = static_cast<float>(animation->mDuration),
        };

        for (int j = 0; j < animation->mNumChannels; j++)
        {
            const aiNodeAnim *animNode = animation->mChannels[j];
            const aiNode *node = scene->mRootNode->FindNode(animNode->mNodeName);
            const uint32_t nodeIndex = sceneNodeIndices.at(node);

            AnimationNode outAnimNode(nodeIndex);

            outAnimNode.Positions.Keys.reserve(animNode->mNumPositionKeys);
            outAnimNode.Rotations.Keys.reserve(animNode->mNumRotationKeys);
            outAnimNode.Scales.Keys.reserve(animNode->mNumScalingKeys);

            {
                aiQuaternion rotation;
                aiVector3D position, scaling;
                node->mTransformation.Decompose(scaling, rotation, position);

                if (animNode->mPositionKeys[0].mTime != 0.0f)
                    outAnimNode.Positions.Keys.emplace_back(
                        TrivialCopy<aiVector3D, glm::vec3>(position), 0.0f
                    );
                if (animNode->mRotationKeys[0].mTime != 0.0f)
                    outAnimNode.Rotations.Keys.emplace_back(
                        glm::quat(rotation.w, rotation.x, rotation.y, rotation.z), 0.0f
                    );
                if (animNode->mScalingKeys[0].mTime != 0.0f)
                    outAnimNode.Scales.Keys.emplace_back(TrivialCopy<aiVector3D, glm::vec3>(position), 0.0f);
            }

            for (int k = 0; k < animNode->mNumPositionKeys; k++)
            {
                const aiVectorKey *key = &animNode->mPositionKeys[k];
                assert(key->mInterpolation == aiAnimInterpolation_Linear);

                outAnimNode.Positions.Keys.emplace_back(
                    TrivialCopy<aiVector3D, glm::vec3>(key->mValue), static_cast<float>(key->mTime)
                );
            }

            for (int k = 0; k < animNode->mNumRotationKeys; k++)
            {
                const aiQuatKey *key = &animNode->mRotationKeys[k];
                assert(
                    key->mInterpolation == aiAnimInterpolation_Linear ||
                    key->mInterpolation == aiAnimInterpolation_Spherical_Linear
                );

                outAnimNode.Rotations.Keys.emplace_back(
                    glm::quat(key->mValue.w, key->mValue.x, key->mValue.y, key->mValue.z),
                    static_cast<float>(key->mTime)
                );
            }

            for (int k = 0; k < animNode->mNumScalingKeys; k++)
            {
                const aiVectorKey *key = &animNode->mScalingKeys[k];
                assert(key->mInterpolation == aiAnimInterpolation_Linear);

                outAnimNode.Scales.Keys.emplace_back(
                    TrivialCopy<aiVector3D, glm::vec3>(key->mValue), static_cast<float>(key->mTime)
                );
            }

            assert(
                animNode->mPreState == aiAnimBehaviour_DEFAULT ||
                animNode->mPreState == aiAnimBehaviour_REPEAT
            );
            assert(
                animNode->mPostState == aiAnimBehaviour_DEFAULT || animNode->mPostState == aiAnimBehaviour_REPEAT
            );

            outAnimation.Nodes.push_back(std::move(outAnimNode));
        }

        sceneBuilder.AddAnimation(std::move(outAnimation));
    }
}

void LoadLights(SceneBuilder &sceneBuilder, const aiScene *scene)
{
    for (int i = 0; i < scene->mNumLights; i++)
    {
        const aiLight *light = scene->mLights[i];

        assert(light->mType == aiLightSource_POINT);
        assert(light->mColorAmbient == light->mColorDiffuse && light->mColorDiffuse == light->mColorSpecular);

        logger::info("Light {} ({})", light->mName.C_Str(), static_cast<uint32_t>(light->mType));
        logger::info(
            "Light Color ({}, {}, {})", light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b
        );
        logger::info(
            "Light Attenuation ({}, {}, {})", light->mAttenuationConstant, light->mAttenuationLinear,
            light->mAttenuationQuadratic
        );

        aiNode *rootNode = scene->mRootNode;
        aiNode *cameraNode = rootNode->FindNode(light->mName);
        aiMatrix4x4 cameraTransformationMatrix = cameraNode->mTransformation;
        aiQuaternion rotation;
        aiVector3D position;
        cameraTransformationMatrix.DecomposeNoScaling(rotation, position);

        sceneBuilder.AddLight({
            .Color = light->mColorDiffuse.IsBlack()
                         ? glm::vec3(1.0f)
                         : TrivialCopyUnsafe<aiColor3D, glm::vec3>(light->mColorDiffuse),
            .Position = TrivialCopy<aiVector3D, glm::vec3>(position),
            .AttenuationConstant = light->mAttenuationConstant,
            .AttenuationLinear = light->mAttenuationLinear,
            .AttenuationQuadratic = light->mAttenuationQuadratic,
        });
    }
}

void LoadCameras(SceneBuilder &sceneBuilder, const aiScene *scene)
{
    for (int i = 0; i < scene->mNumCameras; i++)
    {
        const aiCamera *camera = scene->mCameras[i];

        // find the transformation matrix corresponding to the camera node
        aiNode *rootNode = scene->mRootNode;
        aiNode *cameraNode = rootNode->FindNode(camera->mName);
        aiMatrix4x4 cameraTransformationMatrix = cameraNode->mTransformation;
        aiQuaternion rotation;
        aiVector3D position;
        cameraTransformationMatrix.DecomposeNoScaling(rotation, position);

        glm::quat rotation2 = { rotation.w, rotation.x, rotation.y, rotation.z };
        float yaw = glm::degrees(glm::yaw(rotation2));
        float pitch = glm::degrees(glm::pitch(rotation2));

        glm::vec3 position2 = TrivialCopyUnsafe<aiVector3D, glm::vec3>(position);

        Camera sceneCamera(
            glm::degrees(camera->mHorizontalFOV), camera->mClipPlaneNear, camera->mClipPlaneFar,
            std::move(position2), yaw, pitch
        );

        logger::info(
            "Camera {} Position ({}, {}, {})", camera->mName.C_Str(), position2.x, position2.y, position2.z
        );
        logger::info("Camera {} Yaw ({}) Pitch ({})", camera->mName.C_Str(), yaw, pitch);

        sceneBuilder.AddCamera(std::move(sceneCamera));
    }
}

}

std::shared_ptr<Scene> AssetImporter::LoadScene(const std::string &name, const std::filesystem::path &path)
{
    logger::info("Loading Scene {}", name);
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

    logger::info("Number of meshes in the scene: {}", scene->mNumMeshes);
    logger::info("Number of materials in the scene: {}", scene->mNumMaterials);
    logger::info("Number of lights in the scene: {}", scene->mNumLights);
    logger::info("Number of cameras in the scene: {}", scene->mNumCameras);

    // TODO: Support embedded textures
    assert(scene->HasTextures() == false);

    SceneBuilder sceneBuilder;

    std::vector<uint32_t> materialIndexMap = LoadMaterials(path, sceneBuilder, scene);
    std::vector<uint32_t> meshToGeometry = LoadMeshes(sceneBuilder, path, scene);
    std::unordered_set<const aiNode *> dynamicNodes = FindDynamicNodes(scene);
    std::unordered_map<const aiNode *, uint32_t> sceneNodeIndices =
        LoadSceneHierarchy(sceneBuilder, scene, meshToGeometry, materialIndexMap, dynamicNodes);

    LoadAnimations(sceneBuilder, scene, sceneNodeIndices);
    
    LoadLights(sceneBuilder, scene);
    LoadCameras(sceneBuilder, scene);

    return sceneBuilder.CreateSceneShared(name);
}

}
