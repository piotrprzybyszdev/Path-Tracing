#include "ShaderRendererTypes.incl"

const float ambient = 0.05f;

uint GetColorTextureIndex(uint HitGroupFlags, MetalicRoughnessMaterial material)
{
    return (HitGroupFlags & HitGroupFlagsDisableColorTexture) == 0 ? material.ColorIdx : DefaultColorTextureIndex;
}

uint GetNormalTextureIndex(uint HitGroupFlags, MetalicRoughnessMaterial material)
{
    return (HitGroupFlags & HitGroupFlagsDisableNormalTexture) == 0 ? material.NormalIdx : DefaultNormalTextureIndex;
}

uint GetRoughnessTextureIndex(uint HitGroupFlags, MetalicRoughnessMaterial material)
{
    return (HitGroupFlags & HitGroupFlagsDisableRoughnessTexture) == 0 ? material.RoughnessIdx : DefaultRoughnessTextureIndex;
}

uint GetMetalicTextureIndex(uint HitGroupFlags, MetalicRoughnessMaterial material)
{
    return (HitGroupFlags & HitGroupFlagsDisableMetalicTexture) == 0 ? material.MetalicIdx : DefaultMetalicTextureIndex;
}

uint hash(uint x)
{
    x *= 0x1eca7d79u;
    x ^= x >> 20;
    x = (x << 8) | (x >> 24);
    x = ~x;
    x ^= x << 5;
    x += 0x10afe4e7u;
    return x;
}

vec3 getRandomColor(uint x)
{
    uint rand = hash(x);
    float r = ((rand & 0xff000000) >> 24) / 255.0f;
    float g = ((rand & 0x00ff0000) >> 16) / 255.0f;
    float b = ((rand & 0x0000ff00) >> 8) / 255.0f;

    return vec3(r, g, b);
}
