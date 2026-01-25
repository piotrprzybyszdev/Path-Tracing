#pragma once

#include <filesystem>
#include <variant>

#include "Scene.h"

namespace PathTracing
{

struct MetallicRoughnessTextureMapping
{
    TextureType ColorTexture;
    TextureType NormalTexture;
    TextureType RoughnessTexture;
    TextureType MetallicTexture;
};

struct SpecularGlossinessTextureMapping
{
    TextureType ColorTexture;
    TextureType NormalTexture;
    TextureType SpecularTexture;
    TextureType GlossinessTexture;
};

struct PhongTextureMapping
{
    TextureType ColorTexture;
    TextureType NormalTexture;
    TextureType SpecularTexture;
    TextureType ShininessTexture;
};

using TextureMapping = std::variant<
    std::monostate, MetallicRoughnessTextureMapping, SpecularGlossinessTextureMapping, PhongTextureMapping>;

class SceneImporter
{
public:
    static void Init();
    static void Shutdown();

    static SceneBuilder &AddFile(
        SceneBuilder &builder, const std::filesystem::path &path, TextureMapping mapping = std::monostate()
    );
};

}
