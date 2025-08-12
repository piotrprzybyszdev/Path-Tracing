#pragma once

#include <filesystem>

#include "Scene.h"

namespace PathTracing
{

struct TextureData
{
    int Width, Height, Channels;
    std::span<std::byte> Data;
};

class AssetManager
{
public:
    static TextureData LoadTextureData(const Texture &texture);
    static void ReleaseTextureData(const TextureData &textureData);
    static Scene LoadScene(const std::filesystem::path &path);
};

}
