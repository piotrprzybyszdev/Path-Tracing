#include "ShaderRendererTypes.incl"
#include "Debug/DebugShaderTypes.incl"

uint SampleValue(uint flags, uint disableFlag, uint value, uint defaultValue)
{
    if ((flags & HitGroupFlagsDisableColorTexture) == 0)
        return value;
    return defaultValue;
}

float SampleValue(uint flags, uint disableFlag, float value, float defaultValue)
{
    if ((flags & HitGroupFlagsDisableColorTexture) == 0)
        return value;
    return defaultValue;
}

vec3 SampleValue(uint flags, uint disableFlag, vec3 value, vec3 defaultValue)
{
    if ((flags & HitGroupFlagsDisableColorTexture) == 0)
        return value;
    return defaultValue;
}

struct MaterialSample
{
    vec3 EmissiveColor;
    vec3 Color;
    vec3 Normal;
    float Roughness;
    float Metalness;
};

vec3 ReconstructNormalFromXY(vec3 normal)
{
    // TODO: Don't do it if it's a 3-comp texuture
    normal = 2.0f * normal - 1.0f;
    return vec3(normal.x, normal.y, sqrt(max(1 - normal.x * normal.x - normal.y * normal.y, 0.0f)));
}

MaterialSample SampleMaterial(MetalicRoughnessMaterial material, vec2 texCoords, float lod, uint flags)
{
    MaterialSample ret;

    uint colorIdx = SampleValue(flags, HitGroupFlagsDisableColorTexture, material.ColorIdx, DefaultColorTextureIndex);
    uint normalIdx = SampleValue(flags, HitGroupFlagsDisableNormalTexture, material.NormalIdx, DefaultNormalTextureIndex);
    uint roughnessIdx = SampleValue(flags, HitGroupFlagsDisableRoughnessTexture, material.RoughnessIdx, DefaultRoughnessTextureIndex);
    uint metalicIdx = SampleValue(flags, HitGroupFlagsDisableMetalicTexture, material.MetalicIdx, DefaultMetalicTextureIndex);

    ret.EmissiveColor = (textureLod(textures[material.EmissiveIdx], texCoords, lod).rgb + material.EmissiveColor) * material.EmissiveIntensity;
    ret.Color = textureLod(textures[colorIdx], texCoords, lod).rgb * material.Color;
    ret.Normal = ReconstructNormalFromXY(textureLod(textures[normalIdx], texCoords, lod).rgb);
    ret.Roughness = textureLod(textures[roughnessIdx], texCoords, lod).g * material.Roughness;
    ret.Metalness = textureLod(textures[metalicIdx], texCoords, lod).b * material.Metalness;

    return ret;
}

MaterialSample SampleMaterial(SpecularGlossinessMaterial material, vec2 texCoords, float lod, uint flags)
{
    MaterialSample ret;

    // TODO
    ret.EmissiveColor = vec3(0.0f);
    ret.Color = DefaultColor;
    ret.Normal = DefaultNormal;
    ret.Roughness = DefaultRoughness;
    ret.Metalness = DefaultMetalness;

    return ret;
}

MaterialSample SampleMaterial(uint materialId, vec2 texCoords, float lod, uint flags)
{
    uint materialType;
    uint materialIndex = unpackMaterialId(materialId, materialType);

    if (materialType == MaterialTypeMetalicRoughness)
    {
        return SampleMaterial(metalicRoughnessMaterials[materialIndex], texCoords, lod, flags);
    }
    else
    {
        return SampleMaterial(specularGlossinessMaterials[materialIndex], texCoords, lod, flags);
    }
}
