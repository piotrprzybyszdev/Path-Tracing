#pragma once

#include <string>

#include "Scene.h"

namespace PathTracing
{

class TextureImporter
{
public:
    static TextureInfo GetTextureInfo(TextureSourceVariant source, TextureType type, std::string &&name, bool *hasTransparency = nullptr);
    static TextureData LoadTextureData(const TextureInfo &info);
    static void ReleaseTextureData(const TextureInfo &info, TextureData &data);
};

}
