#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "Scene.h"

namespace PathTracing
{

class AssetManager
{
public:
    static std::byte *LoadTextureData(const TextureInfo &info);
    static void ReleaseTextureData(std::byte *data);

    static void AddScene(const std::string &name, Scene &&scene);
    static void LoadScene(const std::string &name, const std::filesystem::path &path);
    static const Scene &GetScene(const std::string &name);
    static void ReleaseScene(const std::string &name);

public:
    static TextureInfo GetTextureInfo(const std::filesystem::path path, TextureType type);

private:
    static std::unordered_map<std::string, Scene> s_Scenes;
};

}
