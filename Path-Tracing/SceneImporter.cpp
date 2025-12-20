#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/ext/matrix_relational.hpp>

#include <stack>

#include "Core/Config.h"
#include "Core/Core.h"

#include "Application.h"
#include "SceneImporter.h"
#include "TextureImporter.h"

namespace PathTracing
{

static std::unique_ptr<Assimp::Importer> s_Importer = nullptr;

void SceneImporter::Init()
{
    s_Importer = std::make_unique<Assimp::Importer>();
}

void SceneImporter::Shutdown()
{
    s_Importer.reset();
}

namespace
{

std::span<const aiTextureType> GetTextureTypes(TextureType type)
{
    static std::array<aiTextureType, 2> colorTextureTypes = { aiTextureType_BASE_COLOR,
                                                              aiTextureType_DIFFUSE };
    static std::array<aiTextureType, 1> normalTextureTypes = { aiTextureType_NORMALS };
    static std::array<aiTextureType, 1> roughnessTextureTypes = { aiTextureType_DIFFUSE_ROUGHNESS };
    static std::array<aiTextureType, 1> metalicTextureTypes = { aiTextureType_METALNESS };
    static std::array<aiTextureType, 1> emissiveTextureTypes = { aiTextureType_EMISSIVE };
    static std::array<aiTextureType, 1> specularTextureTypes = { aiTextureType_SPECULAR };

    switch (type)
    {
    case TextureType::Color:
        return colorTextureTypes;
    case TextureType::Normal:
        return normalTextureTypes;
    case TextureType::Roughness:
        return roughnessTextureTypes;
    case TextureType::Metalic:
        return metalicTextureTypes;
    case TextureType::Emisive:
        return emissiveTextureTypes;
    case TextureType::Specular:
        return specularTextureTypes;
    default:
        throw error(std::format("Unsupported texture type {}", static_cast<uint8_t>(type)));
    }
}

uint32_t AddTexture(
    SceneBuilder &sceneBuilder, const std::filesystem::path &base, const aiMaterial *material,
    TextureType type, bool *isTransparent = nullptr
)
{
    for (aiTextureType textureType : GetTextureTypes(type))
    {
        const uint32_t cnt = material->GetTextureCount(textureType);
        if (cnt == 0)
            continue;

        assert(cnt == 1);

        aiString path;
        aiReturn ret = material->GetTexture(textureType, 0, &path);
        assert(ret == aiReturn_SUCCESS);
        logger::trace("Adding texture {} at {}", aiTextureTypeToString(textureType), path.C_Str());
        
        std::filesystem::path texturePath = base / std::filesystem::path(path.C_Str());

        try
        {
            TextureInfo info =
                TextureImporter::GetTextureInfo(texturePath, type, path.C_Str(), isTransparent);
            return sceneBuilder.AddTexture(std::move(info));
        }
        catch (const error &error)
        {
            return Scene::GetDefaultTextureIndex(type);
        }
    }

    return Scene::GetDefaultTextureIndex(type);
}

struct EmissiveInfo
{
    glm::vec3 Color;
    glm::uint TextureIdx;
    float Intensity;
};

EmissiveInfo LoadEmissive(
    const std::filesystem::path &path, SceneBuilder &sceneBuilder, const aiMaterial *material
)
{
    float intensity = 1.0f;
    material->Get(AI_MATKEY_EMISSIVE_INTENSITY, intensity);

    const uint32_t defaultTextureIdx = Scene::GetDefaultTextureIndex(TextureType::Emisive);
    const uint32_t textureIdx = AddTexture(sceneBuilder, path.parent_path(), material, TextureType::Emisive);
    
    if (textureIdx != defaultTextureIdx)
        return EmissiveInfo {
            .Color = glm::vec3(0.0f, 0.0f, 0.0f),
            .TextureIdx = textureIdx,
            .Intensity = intensity,
        };

    aiColor3D color;
    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, color) == aiReturn::aiReturn_SUCCESS)
        return EmissiveInfo {
            .Color = TrivialCopyUnsafe<aiColor3D, glm::vec3>(color),
            .TextureIdx = defaultTextureIdx,
            .Intensity = intensity,
        };

    return EmissiveInfo {
        .Color = glm::vec3(0.0f, 0.0f, 0.0f),
        .TextureIdx = defaultTextureIdx,
        .Intensity = 1.0f,
    };
}

struct MaterialInfo
{
    uint32_t MaterialIndex;
    MaterialType Type;
    bool IsOpaque;
};

MaterialInfo LoadMetalicRoughnessMaterial(
    const std::filesystem::path &path, SceneBuilder &sceneBuilder, const aiMaterial *material,
    const std::string &materialName, MetalicRoughnessTextureMapping mapping
)
{
    aiColor3D color = aiColor3D(1.0f, 1.0f, 1.0f);
    float roughness = 1.0f, metalness = 1.0f;

    material->Get(AI_MATKEY_BASE_COLOR, color);
    material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
    material->Get(AI_MATKEY_METALLIC_FACTOR, metalness);

    EmissiveInfo emissive = LoadEmissive(path, sceneBuilder, material);

    bool hasTransparency;
    Shaders::MetalicRoughnessMaterial outMaterial = {
        .EmissiveColor = emissive.Color,
        .EmissiveIntensity = emissive.Intensity,
        .Color = TrivialCopyUnsafe<aiColor3D, glm::vec3>(color),
        .Roughness = roughness,
        .Metalness = metalness,
        .EmissiveIdx = emissive.TextureIdx,
        .ColorIdx = AddTexture(
            sceneBuilder, path.parent_path(), material, mapping.ColorTexture, &hasTransparency
        ),
        .NormalIdx = AddTexture(sceneBuilder, path.parent_path(), material, mapping.NormalTexture),
        .RoughnessIdx =
            AddTexture(sceneBuilder, path.parent_path(), material, mapping.RoughnessTexture),
        .MetalicIdx = AddTexture(sceneBuilder, path.parent_path(), material, mapping.MetalicTexture),
    };

    return MaterialInfo {
        .MaterialIndex = sceneBuilder.AddMaterial(materialName, outMaterial),
        .Type = MaterialType::MetalicRoughness,
        .IsOpaque = !hasTransparency,
    };
}

// NOTE: Hasn't been tested on an actual specular-glossiness material
MaterialInfo LoadSpecularGlossinessMaterial(
    const std::filesystem::path &path, SceneBuilder &sceneBuilder, const aiMaterial *material,
    const std::string &materialName
)
{
    EmissiveInfo emissive = LoadEmissive(path, sceneBuilder, material);

    bool hasTransparency;
    Shaders::SpecularGlossinessMaterial outMaterial = {
        .EmissiveColor = emissive.Color,
        .EmissiveIntensity = emissive.Intensity,
        .EmissiveIdx = emissive.TextureIdx,
        .DiffuseIdx =
            AddTexture(sceneBuilder, path.parent_path(), material, TextureType::Color, &hasTransparency),
        .NormalIdx = AddTexture(sceneBuilder, path.parent_path(), material, TextureType::Normal),
        .GlossSpecularIdx = AddTexture(sceneBuilder, path.parent_path(), material, TextureType::Specular),
    };

    return MaterialInfo {
        .MaterialIndex = sceneBuilder.AddMaterial(materialName, outMaterial),
        .Type = MaterialType::SpecularGlossiness,
        .IsOpaque = !hasTransparency,
    };
}

std::vector<MaterialInfo> LoadMaterials(
    const std::filesystem::path &path, SceneBuilder &sceneBuilder, const aiScene *scene,
    TextureMapping textureMapping
)
{
    static const MetalicRoughnessTextureMapping defaultMetalicRoughnessMapping = {
        .ColorTexture = TextureType::Color,
        .NormalTexture = TextureType::Normal,
        .RoughnessTexture = TextureType::Roughness,
        .MetalicTexture = TextureType::Metalic,
    };

    const MetalicRoughnessTextureMapping *metalicRoughnessMapping = &defaultMetalicRoughnessMapping;

    std::vector<MaterialInfo> materialInfoMap(scene->mNumMaterials);

    for (int i = 0; i < scene->mNumMaterials; i++)
    {
        const aiMaterial *material = scene->mMaterials[i];
        const aiString originalName = material->GetName();
        const std::string materialName =
            originalName.length != 0 ? originalName.C_Str() : std::format("Unnamed Material at index {}", i);

        float factor;
        MaterialType materialType =
            material->Get(AI_MATKEY_METALLIC_FACTOR, factor) == aiReturn::aiReturn_SUCCESS
                ? MaterialType::MetalicRoughness
                : MaterialType::SpecularGlossiness;

        if (const auto *mapping = std::get_if<MetalicRoughnessTextureMapping>(&textureMapping))
        {
            materialType = MaterialType::MetalicRoughness;
            metalicRoughnessMapping = mapping;
        }

        switch (materialType)
        {
        case MaterialType::MetalicRoughness:
            materialInfoMap[i] = LoadMetalicRoughnessMaterial(
                path, sceneBuilder, material, materialName, *metalicRoughnessMapping
            );
            break;
        case MaterialType::SpecularGlossiness:
            materialInfoMap[i] = LoadSpecularGlossinessMaterial(path, sceneBuilder, material, materialName);
            break;
        default:
            throw error("Unsupported material type");
        }

        logger::debug("Added Material: {}", materialName);
    }

    return materialInfoMap;
}

// Some meshes might differ only in material, but have the same geometry
uint32_t FindSameGeometry(std::span<aiMesh *const> haystack, const aiMesh *needle)
{
    // TODO: Should check that both materials are opaque

    for (uint32_t i = 0; i < haystack.size(); i++)
        if (haystack[i]->mFaces == needle->mFaces && haystack[i]->mNumFaces == needle->mNumFaces &&
            haystack[i]->mBones == needle->mBones)
            return i;

    return haystack.size();
}

bool CheckAnimated(const aiMesh *mesh)
{
    return mesh->HasBones();
}

void LoadBones(
    SceneBuilder &sceneBuilder, const aiScene *scene, std::vector<Shaders::AnimatedVertex> &vertices,
    uint32_t vertexOffset, const aiMesh *mesh,
    const std::unordered_map<const aiNode *, uint32_t> &sceneNodeIndices,
    std::unordered_set<const aiNode *> &armatures
)
{
    std::vector<uint8_t> vertexBoneCount(vertices.size());

    for (int i = 0; i < mesh->mNumBones; i++)
    {
        const aiBone *bone = mesh->mBones[i];
        armatures.insert(bone->mArmature);

        const uint32_t sceneNodeIndex = sceneNodeIndices.at(bone->mNode);
        const uint32_t boneIndex = sceneBuilder.AddBone(
            {
                .SceneNodeIndex = sceneNodeIndex,
                .Offset = TrivialCopy<aiMatrix4x4, glm::mat4>(bone->mOffsetMatrix),
            }
        );

        for (int j = 0; j < bone->mNumWeights; j++)
        {
            const aiVertexWeight &weight = bone->mWeights[j];

            const uint32_t vertexBoneIndex = vertexBoneCount[weight.mVertexId]++;
            vertices[vertexOffset + weight.mVertexId].BoneWeights[vertexBoneIndex] = weight.mWeight;
            vertices[vertexOffset + weight.mVertexId].BoneIndices[vertexBoneIndex] = boneIndex;

            assert(vertexBoneIndex < Shaders::MaxBonesPerVertex);
        }
    }
}

std::vector<uint32_t> LoadMeshes(
    SceneBuilder &sceneBuilder, const std::filesystem::path &path, const aiScene *scene,
    const std::unordered_map<const aiNode *, uint32_t> &sceneNodeIndices,
    const std::vector<MaterialInfo> &materialInfoMap,
    std::unordered_set<const aiNode *> &armatures
)
{
    auto &vertices = sceneBuilder.GetVertices();
    auto &animatedVertices = sceneBuilder.GetAnimatedVertices();
    auto &indices = sceneBuilder.GetIndices();
    auto &animatedIndices = sceneBuilder.GetAnimatedIndices();

    std::vector<uint32_t> meshToGeometry(scene->mNumMeshes);
    uint32_t vertexOffset = vertices.size(), indexOffset = indices.size();
    uint32_t animatedVertexOffset = animatedVertices.size(), animatedIndexOffset = animatedIndices.size();

    {
        uint32_t vertexCount = 0, indexCount = 0;
        uint32_t animatedVertexCount = 0, animatedIndexCount = 0;
        for (int i = 0; i < scene->mNumMeshes; i++)
        {
            const aiMesh *mesh = scene->mMeshes[i];

            uint32_t &vc = CheckAnimated(mesh) ? animatedVertexCount : vertexCount;
            uint32_t &ic = CheckAnimated(mesh) ? animatedIndexCount : indexCount;

            vc += mesh->mNumVertices;
            ic += mesh->mNumFaces * 3;
        }

        vertices.resize(vertices.size() + vertexCount);
        animatedVertices.resize(animatedVertices.size() + animatedVertexCount);
        indices.resize(indices.size() + indexCount);
        animatedIndices.resize(animatedIndices.size() + animatedIndexCount);
    }

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

            Application::IncrementBackgroundTaskDone(BackgroundTaskType::SceneImport);
            continue;
        }

        const uint32_t vertexCount = mesh->mNumVertices;
        const uint32_t indexCount = mesh->mNumFaces * 3;

        uint32_t &vo = CheckAnimated(mesh) ? animatedVertexOffset : vertexOffset;
        uint32_t &io = CheckAnimated(mesh) ? animatedIndexOffset : indexOffset;

        assert(!mesh->HasTextureCoords(0) || mesh->mNumUVComponents[0] == 2);

        for (int j = 0; j < vertexCount; j++)
        {
            const uint32_t idx = vo + j;

            auto computeTangentSpace = [](glm::vec3 normal) -> std::pair<glm::vec3, glm::vec3> {
                glm::vec3 t1 = glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 t2 = glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f));

                glm::vec3 tangent = glm::length(t1) > glm::length(t2) ? t1 : t2;
                glm::vec3 bitangent = cross(normal, tangent);
                
                return { glm::normalize(tangent), glm::normalize(bitangent) };
            };

            if (CheckAnimated(mesh))
            {
                animatedVertices[idx].Position = TrivialCopy<aiVector3D, glm::vec3>(mesh->mVertices[j]);
                if (mesh->HasTextureCoords(0))
                    animatedVertices[idx].TexCoords =
                        TrivialCopy<aiVector3D, glm::vec2>(mesh->mTextureCoords[0][j]);
                animatedVertices[idx].Normal = TrivialCopy<aiVector3D, glm::vec3>(mesh->mNormals[j]);
                if (mesh->HasTangentsAndBitangents())
                {
                    animatedVertices[idx].Tangent = TrivialCopy<aiVector3D, glm::vec3>(mesh->mTangents[j]);
                    animatedVertices[idx].Bitangent =
                        TrivialCopy<aiVector3D, glm::vec3>(mesh->mBitangents[j]);
                }
                else
                {                 
                    std::tie(animatedVertices[idx].Tangent, animatedVertices[idx].Bitangent) =
                        computeTangentSpace(animatedVertices[idx].Normal);
                }
            }
            else
            {
                vertices[idx].Position = TrivialCopy<aiVector3D, glm::vec3>(mesh->mVertices[j]);
                if (mesh->HasTextureCoords(0))
                    vertices[idx].TexCoords = TrivialCopy<aiVector3D, glm::vec2>(mesh->mTextureCoords[0][j]);
                vertices[idx].Normal = TrivialCopy<aiVector3D, glm::vec3>(mesh->mNormals[j]);
                if (mesh->HasTangentsAndBitangents())
                {
                    vertices[idx].Tangent = TrivialCopy<aiVector3D, glm::vec3>(mesh->mTangents[j]);
                    vertices[idx].Bitangent = TrivialCopy<aiVector3D, glm::vec3>(mesh->mBitangents[j]);
                }
                else
                {
                    std::tie(vertices[idx].Tangent, vertices[idx].Bitangent) =
                        computeTangentSpace(vertices[idx].Normal);
                }
            }
        }

        for (int j = 0; j < mesh->mNumFaces; j++)
        {
            const uint32_t idx = io + j * 3;
            const aiFace &face = mesh->mFaces[j];
            assert(face.mNumIndices == 3);
            std::vector<uint32_t> &i = !CheckAnimated(mesh) ? indices : animatedIndices;
            std::ranges::copy(std::span(face.mIndices, 3), i.begin() + idx);
        }

        if (CheckAnimated(mesh))
            LoadBones(sceneBuilder, scene, animatedVertices, vo, mesh, sceneNodeIndices, armatures);

        bool isOpaque = materialInfoMap[mesh->mMaterialIndex].IsOpaque;

        if (!CheckAnimated(mesh))
            meshToGeometry[i] =
                sceneBuilder.AddGeometry({ vo, vertexCount, io, indexCount, isOpaque, false });
        else
            meshToGeometry[i] = sceneBuilder.AddGeometry({ vo, vertexCount, io, indexCount, isOpaque, true });

        vo += vertexCount;
        io += indexCount;

        logger::debug(
            "Adding geometry (mesh {}) ({}) with {} vertices and {} indices", mesh->mName.C_Str(),
            isOpaque ? "Opaque" : "Not opaque", vertexCount, indexCount
        );

        Application::IncrementBackgroundTaskDone(BackgroundTaskType::SceneImport);
    }

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
            logger::debug("{} ({:.1f}s)", animationName, animation->mDuration / animation->mTicksPerSecond);
        }

        for (int j = 0; j < animation->mNumChannels; j++)
        {
            const aiNodeAnim *animNode = animation->mChannels[j];
            logger::debug("    animates node named: {}", animNode->mNodeName.C_Str());

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

std::unordered_map<const aiNode *, uint32_t> LoadSceneNodes(
    SceneBuilder &sceneBuilder, const aiScene *scene, std::vector<const aiNode *> &nodes
)
{
    std::unordered_map<const aiNode *, uint32_t> sceneNodeToIndex;

    std::stack<std::tuple<const aiNode *, uint32_t, uint32_t>> stack;
    stack.emplace(scene->mRootNode, SceneBuilder::RootNodeIndex, 0u);

    while (!stack.empty())
    {
        auto [node, parentNodeIndex, depth] = stack.top();
        stack.pop();

        nodes.push_back(node);

        logger::debug(
            "{}{}, mesh count: {}", std::string(depth * 4, ' '), node->mName.C_Str(), node->mNumMeshes
        );

        const uint32_t sceneNodeIndex = sceneBuilder.AddSceneNode(
            {
                .Parent = parentNodeIndex,
                .Transform = TrivialCopy<aiMatrix4x4, glm::mat4>(node->mTransformation),
                .CurrentTransform = glm::mat4(1.0f),
            }
        );

        sceneNodeToIndex.emplace(node, sceneNodeIndex);

        for (int i = 0; i < node->mNumChildren; i++)
            stack.emplace(node->mChildren[i], sceneNodeIndex, depth + 1);
    }

    return sceneNodeToIndex;
}

void LoadModels(
    SceneBuilder &sceneBuilder, const aiScene *scene,
    std::unordered_map<const aiNode *, uint32_t> sceneNodeIndices,
    std::unordered_set<const aiNode *> dynamicNodes, std::unordered_set<const aiNode *> armatures,
    std::vector<const aiNode *> nodes, const std::vector<MaterialInfo> &materialInfoMap,
    const std::vector<uint32_t> &meshToGeometry
)
{
    const uint32_t maxInstanceCount = 1 + dynamicNodes.size() + armatures.size();
    auto isInstanceRoot = [&](const aiNode *node) {
        return dynamicNodes.contains(node) || scene->mRootNode == node;
    };

    std::vector<std::vector<MeshInfo>> modelToMeshInfos(maxInstanceCount);
    std::vector<std::vector<MeshInfo>> modelToAnimatedMeshInfos(maxInstanceCount);
    std::vector<uint32_t> modelToSceneNodeIndex(maxInstanceCount);

    uint32_t nextModelIndex = 0;

    std::vector<uint32_t> sceneNodeToModelIndex(nodes.size());
    std::vector<glm::mat4> sceneNodeToMeshTransform(nodes.size());

    auto getNodeIndex = [&sceneNodeIndices, scene](const aiNode *node) {
        return sceneNodeIndices[node] - sceneNodeIndices[scene->mRootNode];
    };

    for (const aiNode *node : nodes)
    {
        const uint32_t sceneNodeIndex = getNodeIndex(node);

        uint32_t &modelIndex = sceneNodeToModelIndex[sceneNodeIndex];
        glm::mat4 &totalTransform = sceneNodeToMeshTransform[sceneNodeIndex];

        if (isInstanceRoot(node))
        {
            modelIndex = nextModelIndex++;
            modelToSceneNodeIndex[modelIndex] = sceneNodeIndices[node];
            totalTransform = glm::mat4(1.0f);
        }
        else
        {
            const uint32_t parentNodeIndex = getNodeIndex(node->mParent);
            modelIndex = sceneNodeToModelIndex[parentNodeIndex];
            totalTransform = TrivialCopy<aiMatrix4x4, glm::mat4>(node->mTransformation) *
                             sceneNodeToMeshTransform[parentNodeIndex];
        }

        bool hasAnimatedMeshes = false;
        for (int i = 0; i < node->mNumMeshes; i++)
        {
            const uint32_t meshIndex = node->mMeshes[i];
            const aiMesh *mesh = scene->mMeshes[meshIndex];

            if (CheckAnimated(mesh))
            {
                hasAnimatedMeshes = true;
                continue;
            }

            auto [materialIndex, materialType, _] = materialInfoMap[mesh->mMaterialIndex];

            modelToMeshInfos[modelIndex].emplace_back(
                meshToGeometry[meshIndex], materialIndex, materialType, totalTransform
            );
        }

        uint32_t animatedModelIndex = -1;

        if (hasAnimatedMeshes)
        {
            // For animated meshes we create another instance
            animatedModelIndex = nextModelIndex++;

            // We assume that the direct parent of the mesh is an ancestor of the mesh's armature
            // (Lowest common ancestor of both node with the mesh and armature node)
            const aiNode *ancestor = node->mParent;

            // We can find the transformation of the vertices in two ways:
            // (ancestor absolute transform) * (mesh relative transform to ancestor)
            // (ancestor absolute transform) * (mesh relative bone transformation to ancestor)

            // We assert that the mesh relative transform to ancestor is identity
            // Otherwise it is not clear how this transform should interact with the bone transforms
            assert(node->mTransformation.IsIdentity());

            // Ancestor absolute transform is considered to be the instance transform
            // (the ancestor is the root of the instance)
            modelToSceneNodeIndex[animatedModelIndex] = sceneNodeIndices[ancestor];

            // We set the bone transforms to be relative the the ancestor
            for (int j = 0; j < node->mParent->mNumChildren; j++)
                sceneBuilder.SetAbsoluteTransform(sceneNodeIndices.at(node->mParent->mChildren[j]));
        }

        for (int i = 0; i < node->mNumMeshes; i++)
        {
            const uint32_t meshIndex = node->mMeshes[i];
            const aiMesh *mesh = scene->mMeshes[meshIndex];

            // Assert that the mesh's armature is in fact the child of the assumed ancestor
            const aiNode *ancestor = node->mParent;
            for (int j = 0; j < mesh->mNumBones; j++)
                assert(ancestor->FindNode(mesh->mBones[j]->mArmature->mName) != nullptr);

            if (CheckAnimated(mesh))
            {
                auto [materialIndex, materialType, _] = materialInfoMap[mesh->mMaterialIndex];

                modelToAnimatedMeshInfos[animatedModelIndex].emplace_back(
                    meshToGeometry[meshIndex], materialIndex, materialType, glm::mat4(1.0f)
                );
            }
        }
    }

    // TODO: Combine models into one if their meshInfos are the same

    for (int i = 0; i < modelToMeshInfos.size(); i++)
    {
        if (!modelToMeshInfos[i].empty())
            sceneBuilder.AddModelInstance(
                sceneBuilder.AddModel(modelToMeshInfos[i]), modelToSceneNodeIndex[i]
            );

        if (!modelToAnimatedMeshInfos[i].empty())
            sceneBuilder.AddModelInstance(
                sceneBuilder.AddModel(modelToAnimatedMeshInfos[i]), modelToSceneNodeIndex[i]
            );
    }
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
            .Duration = static_cast<float>(animation->mDuration),
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
                animNode->mPostState == aiAnimBehaviour_DEFAULT ||
                animNode->mPostState == aiAnimBehaviour_REPEAT
            );

            outAnimation.Nodes.push_back(std::move(outAnimNode));
        }

        sceneBuilder.AddAnimation(std::move(outAnimation));
        Application::IncrementBackgroundTaskDone(BackgroundTaskType::SceneImport);
    }
}

void LoadLights(
    SceneBuilder &sceneBuilder, const aiScene *scene,
    const std::unordered_map<const aiNode *, uint32_t> &sceneNodeIndices
)
{
    for (int i = 0; i < scene->mNumLights; i++)
    {
        const aiLight *light = scene->mLights[i];
        bool hasDirectionalLight = false;

        assert(
            light->mType == aiLightSourceType::aiLightSource_POINT ||
            light->mType == aiLightSourceType::aiLightSource_DIRECTIONAL
        );
        assert(
            (light->mColorAmbient == light->mColorDiffuse || light->mColorAmbient.IsBlack()) &&
            light->mColorDiffuse == light->mColorSpecular
        );

        logger::debug("Light {} ({})", light->mName.C_Str(), static_cast<uint32_t>(light->mType));
        logger::debug(
            "Light Color ({}, {}, {})", light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b
        );

        const aiNode *node = scene->mRootNode->FindNode(light->mName);
        const uint32_t nodeIndex = sceneNodeIndices.at(node);

        switch (light->mType)
        {
        case aiLightSourceType::aiLightSource_POINT:
            sceneBuilder.AddLight(
                {
                    .Color = light->mColorDiffuse.IsBlack()
                                 ? glm::vec3(1.0f)
                                 : TrivialCopyUnsafe<aiColor3D, glm::vec3>(light->mColorDiffuse),
                    .Position = TrivialCopy<aiVector3D, glm::vec3>(light->mPosition),
                    .AttenuationConstant = light->mAttenuationConstant,
                    .AttenuationLinear = light->mAttenuationLinear,
                    .AttenuationQuadratic = light->mAttenuationQuadratic,
                },
                nodeIndex
            );
            break;
        case aiLightSourceType::aiLightSource_DIRECTIONAL:
            if (hasDirectionalLight)
            {
                logger::warn(
                    "Only one directional light per scene is supported, ignoring light {}",
                    light->mName.C_Str()
                );
                break;
            }

            sceneBuilder.SetDirectionalLight(
                { .Color = light->mColorDiffuse.IsBlack()
                               ? glm::vec3(1.0f)
                               : TrivialCopyUnsafe<aiColor3D, glm::vec3>(light->mColorDiffuse),
                  .Direction = TrivialCopy<aiVector3D, glm::vec3>(light->mDirection) },
                nodeIndex
            );
            hasDirectionalLight = true;
            break;
        default:
            throw error(std::format("Unuspported light type: {}", static_cast<uint32_t>(light->mType)));
        }
    }
}

void LoadCameras(
    SceneBuilder &sceneBuilder, const aiScene *scene,
    const std::unordered_map<const aiNode *, uint32_t> &sceneNodeIndices
)
{
    for (int i = 0; i < scene->mNumCameras; i++)
    {
        const aiCamera *camera = scene->mCameras[i];

        const aiNode *node = scene->mRootNode->FindNode(camera->mName);
        const uint32_t nodeIndex = sceneNodeIndices.at(node);

        const float aspect = camera->mAspect == 0.0f ? 16.0f / 9.0f : camera->mAspect;
        const float verticalFov = camera->mHorizontalFOV == 0.0f
                                      ? 45.0f
                                      : (2.0f * glm::atan(glm::tan(camera->mHorizontalFOV / 2.0f) * aspect));
        glm::vec3 up = TrivialCopy<aiVector3D, glm::vec3>(camera->mUp);
        up.y *= -1;

        sceneBuilder.AddCamera(
            CameraInfo {
                .VerticalFOV = glm::degrees(verticalFov),
                .NearClip = camera->mClipPlaneNear,
                .FarClip = camera->mClipPlaneFar,
                .Position = TrivialCopy<aiVector3D, glm::vec3>(camera->mPosition),
                .Direction = TrivialCopy<aiVector3D, glm::vec3>(camera->mLookAt),
                .UpDirection = up,
                .SceneNodeIndex = nodeIndex,
            }
        );
    }
}

class ProgressHandler : public Assimp::ProgressHandler
{
public:
    ProgressHandler(uint32_t taskTotal) : m_TaskTotal(taskTotal)
    {
    }

    ~ProgressHandler() override = default;

    bool Update(float percentage) override
    {
        const uint32_t done = m_TaskTotal * percentage;
        Application::IncrementBackgroundTaskDone(BackgroundTaskType::SceneImport, done - m_PreviousDone);
        m_PreviousDone = done;

        return true;
    }

private:
    const uint32_t m_TaskTotal;
    uint32_t m_PreviousDone = 0;
};

}

SceneBuilder &SceneImporter::AddFile(
    SceneBuilder &sceneBuilder, const std::filesystem::path &path, TextureMapping textureMapping
)
{
    // Import by assimp will be half the entire task
    const uint32_t assimpTasks = 100;
    Application::ResetBackgroundTask(BackgroundTaskType::SceneImport);
    Application::AddBackgroundTask(BackgroundTaskType::SceneImport, 2 * assimpTasks);

    logger::info("Loading Scene {}", path.string());
    Timer timer("Scene Load");

    const aiScene *scene = nullptr;
    {
        unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace |
                             aiProcess_LimitBoneWeights | aiProcess_GenNormals |
                             aiProcess_PopulateArmatureData;
#ifdef CONFIG_OPTIMIZE_SCENE
        flags |= aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality | aiProcess_OptimizeMeshes;
#endif

        Timer timer("File Import");
        s_Importer->SetProgressHandler(new ProgressHandler(assimpTasks));
        scene = s_Importer->ReadFile(path.string().c_str(), flags);

        if (scene == nullptr)
            throw error(s_Importer->GetErrorString());
    }

    assert((scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) == false);
    assert(scene->mRootNode != nullptr);

    logger::info("Number of meshes in the scene: {}", scene->mNumMeshes);
    logger::info("Number of materials in the scene: {}", scene->mNumMaterials);
    logger::info("Number of lights in the scene: {}", scene->mNumLights);
    logger::info("Number of cameras in the scene: {}", scene->mNumCameras);
    logger::info("Number of animations in the scene: {}", scene->mNumAnimations);

    // Report half of the task as done
    const uint32_t taskSize = scene->mNumMeshes + scene->mNumAnimations;
    Application::AddBackgroundTask(BackgroundTaskType::SceneImport, 2 * taskSize - assimpTasks);
    Application::IncrementBackgroundTaskDone(BackgroundTaskType::SceneImport, taskSize);

    // TODO: Support embedded textures
    assert(scene->HasTextures() == false);

    std::vector<const aiNode *> nodes;
    std::unordered_map<const aiNode *, uint32_t> sceneNodeIndices =
        LoadSceneNodes(sceneBuilder, scene, nodes);
    std::vector<MaterialInfo> materialInfoMap = LoadMaterials(path, sceneBuilder, scene, textureMapping);

    std::unordered_set<const aiNode *> armatures;
    std::vector<uint32_t> meshToGeometry =
        LoadMeshes(sceneBuilder, path, scene, sceneNodeIndices, materialInfoMap, armatures);
    std::unordered_set<const aiNode *> dynamicNodes = FindDynamicNodes(scene);

    LoadModels(
        sceneBuilder, scene, sceneNodeIndices, dynamicNodes, armatures, nodes, materialInfoMap, meshToGeometry
    );

    LoadAnimations(sceneBuilder, scene, sceneNodeIndices);

    LoadLights(sceneBuilder, scene, sceneNodeIndices);
    LoadCameras(sceneBuilder, scene, sceneNodeIndices);

    return sceneBuilder;
}

}
