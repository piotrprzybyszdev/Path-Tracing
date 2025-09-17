#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

#include "Scene.h"

namespace PathTracing
{

class AssetImporter
{
public:
    static std::byte *LoadTextureData(const TextureInfo &info);
    static void ReleaseTextureData(std::byte *data);

    static std::shared_ptr<Scene> LoadScene(const std::string &name, const std::filesystem::path &path);

public:
    static TextureInfo GetTextureInfo(const std::filesystem::path path, TextureType type);
};

}
