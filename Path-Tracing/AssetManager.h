#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "Scene.h"

namespace PathTracing
{

struct Texture
{
    int Width, Height, Channels;
    std::byte *Data;
};

class AssetManager
{
public:
    static void LoadTexture(const std::filesystem::path &path);
    static const Texture &GetTexture(const std::filesystem::path &path);
    static void ReleaseTexture(const std::filesystem::path &path);

    static void AddScene(const std::string &name, Scene &&scene);
    static void LoadScene(const std::string &name, const std::filesystem::path &path);
    static const Scene &GetScene(const std::string &name);
    static void ReleaseScene(const std::string &name);

private:
    static std::unordered_map<std::filesystem::path, Texture> s_Textures;
    static std::unordered_map<std::string, Scene> s_Scenes;
};

}
