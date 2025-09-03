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
    static Texture LoadTexture(const std::filesystem::path &path);
    static void ReleaseTexture(Texture texture);

    static void AddScene(const std::string &name, Scene &&scene);
    static void LoadScene(const std::string &name, const std::filesystem::path &path);
    static const Scene &GetScene(const std::string &name);
    static void ReleaseScene(const std::string &name);

private:
    static std::unordered_map<std::string, Scene> s_Scenes;
};

}
