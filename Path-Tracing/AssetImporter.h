#pragma once

#include <filesystem>
#include <string>

#include "Scene.h"

namespace PathTracing
{

class AssetImporter
{
public:
    static void Init();
    static void Shutdown();

    static std::byte *LoadTextureData(const TextureInfo &info);
    static void ReleaseTextureData(std::byte *data);

    static SceneBuilder &AddFile(SceneBuilder &builder, const std::filesystem::path &path);

public:
    static TextureInfo GetTextureInfo(TextureSourceVariant source, TextureType type, std::string &&name);
};

}
