#pragma once

#include <string>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "Renderer/Image.h"

namespace PathTracing
{

class MaterialSystem
{
public:
    struct Material
    {
        uint32_t albedoIdx, normalIdx, roughnessIdx, metalicIdx;
    };

    static void Init();
    static void Shutdown();

    static uint32_t AddMaterial(
        std::string name, std::filesystem::path albedo, std::filesystem::path normal,
        std::filesystem::path roughness, std::filesystem::path metalic
    );

    static const std::vector<Image> &GetTextures();
    static const std::vector<Material> &GetMaterials();

    static void UploadBuffer();
    static const Buffer &GetBuffer();

private:
    static std::vector<Image> s_Textures;
    static std::unordered_map<std::string, uint32_t> s_TextureIndices;

    static std::vector<Material> s_Materials;
    static std::unordered_map<std::string, uint32_t> s_MaterialIndices;

    static std::unique_ptr<Buffer> s_MaterialBuffer;

private:
    static uint32_t AddTexture(std::filesystem::path path);
};

}
