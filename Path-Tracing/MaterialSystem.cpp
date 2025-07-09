#include <stb_image.h>

#include "Core/Core.h"

#include "Renderer/Buffer.h"

#include "MaterialSystem.h"

namespace PathTracing
{

std::vector<Image> MaterialSystem::s_Textures = {};
std::unordered_map<std::string, uint32_t> MaterialSystem::s_TextureIndices = {};
std::vector<MaterialSystem::Material> MaterialSystem::s_Materials = {};
std::unordered_map<std::string, uint32_t> MaterialSystem::s_MaterialIndices = {};

std::unique_ptr<Buffer> MaterialSystem::s_MaterialBuffer = nullptr;

void MaterialSystem::Init()
{
}

void MaterialSystem::Shutdown()
{
    s_MaterialBuffer.reset();

    s_Textures.clear();
    s_TextureIndices.clear();
    s_Materials.clear();
    s_MaterialIndices.clear();
}

uint32_t MaterialSystem::AddMaterial(
    std::string name, std::filesystem::path albedo, std::filesystem::path normal,
    std::filesystem::path roughness, std::filesystem::path metalic
)
{
    if (s_MaterialIndices.contains(name))
        return s_MaterialIndices.contains(name);

    s_Materials.emplace_back(Material {
        AddTexture(albedo),
        AddTexture(normal),
        AddTexture(roughness),
        AddTexture(metalic),
    });

    s_MaterialIndices[name] = s_Materials.size() - 1;
    return s_Materials.size() - 1;
}

const std::vector<Image> &MaterialSystem::GetTextures()
{
    return s_Textures;
}

const std::vector<MaterialSystem::Material> &MaterialSystem::GetMaterials()
{
    return s_Materials;
}

void MaterialSystem::UploadBuffer()
{
    BufferBuilder builder;
    builder.SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal)
        .SetUsageFlags(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);

    s_MaterialBuffer = builder.CreateBufferUnique(s_Materials.size() * sizeof(Material));
    s_MaterialBuffer->Upload(s_Materials.data());
}

const Buffer &MaterialSystem::GetBuffer()
{
    return *s_MaterialBuffer;
}

uint32_t MaterialSystem::AddTexture(std::filesystem::path path)
{
    // TODO: Support other texture formats than R8B8G8A8Unorm

    std::string textureName = path.string();

    if (s_TextureIndices.contains(textureName))
        return s_TextureIndices.contains(textureName);

    int x, y, channels;
    stbi_uc *data = stbi_load(textureName.c_str(), &x, &y, &channels, STBI_rgb_alpha);

    if (data == nullptr)
        throw error(std::format("Could not load texture {}: {}", textureName, stbi_failure_reason()));

    ImageBuilder builder;
    builder.SetFormat(vk::Format::eR8G8B8A8Unorm)
        .SetUsageFlags(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst)
        .SetMemoryFlags(vk::MemoryPropertyFlagBits::eDeviceLocal);

    s_Textures.emplace_back(builder.CreateImage(vk::Extent2D(x, y)));
    s_Textures.back().UploadStaging(static_cast<uint8_t*>(data));
    stbi_image_free(data);

    s_TextureIndices[textureName] = s_Textures.size() - 1;
    return s_Textures.size() - 1;
}

}