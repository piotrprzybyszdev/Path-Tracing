#include "ShaderRendererTypes.incl"
#include "Debug/DebugShaderTypes.incl"

uint sampleValue(uint flags, uint disableFlag, uint value, uint defaultValue)
{
    if ((flags & disableFlag) == 0)
        return value;
    return defaultValue;
}

float sampleValue(uint flags, uint disableFlag, float value, float defaultValue)
{
    if ((flags & disableFlag) == 0)
        return value;
    return defaultValue;
}

vec3 sampleValue(uint flags, uint disableFlag, vec3 value, vec3 defaultValue)
{
    if ((flags & disableFlag) == 0)
        return value;
    return defaultValue;
}

vec3 ReconstructNormalFromXY(vec3 normal)
{
    // TODO: Don't do it if it's a 3-comp texuture
    normal = 2.0f * normal - 1.0f;
    return vec3(normal.x, normal.y, sqrt(max(1 - normal.x * normal.x - normal.y * normal.y, 0.0f)));
}

MaterialSample sampleMaterial(MetallicRoughnessMaterial material, vec2 texCoords, vec4 derivatives, bool isHitFromInside, uint flags)
{
    MaterialSample ret;

    vec2 dpdx = derivatives.xy;
    vec2 dpdy = derivatives.zw;

    uint colorIdx = sampleValue(flags, HitGroupFlagsDisableColorTexture, material.ColorIdx, DefaultColorTextureIndex);
    uint normalIdx = sampleValue(flags, HitGroupFlagsDisableNormalTexture, material.NormalIdx, DefaultNormalTextureIndex);
    uint roughnessIdx = sampleValue(flags, HitGroupFlagsDisableRoughnessTexture, material.RoughnessIdx, DefaultRoughnessTextureIndex);
    uint metallicIdx = sampleValue(flags, HitGroupFlagsDisableMetallicTexture, material.MetallicIdx, DefaultMetallicTextureIndex);

    ret.EmissiveColor = (textureGrad(textures[material.EmissiveIdx], texCoords, dpdx, dpdy).rgb + material.EmissiveColor) * material.EmissiveIntensity;
    ret.Color = textureGrad(textures[colorIdx], texCoords, dpdx, dpdy).rgb * material.Color.rgb;
    ret.Normal = ReconstructNormalFromXY(textureGrad(textures[normalIdx], texCoords, dpdx, dpdy).rgb);
    ret.Roughness = textureGrad(textures[roughnessIdx], texCoords, dpdx, dpdy).g * material.Roughness;
    ret.Metalness = textureGrad(textures[metallicIdx], texCoords, dpdx, dpdy).b * material.Metalness;
    ret.Transmission = material.Transmission;

    ret.Eta = isHitFromInside ? material.Ior : (1.0f / material.Ior);

    return ret;
}

MaterialSample sampleMaterial(SpecularGlossinessMaterial material, vec2 texCoords, vec4 derivatives, bool isHitFromInside, uint flags)
{
    MaterialSample ret;

    // TODO
    ret.EmissiveColor = vec3(1.0f, 0.0f, 1.0f);
    ret.Color = DefaultColor;
    ret.Normal = DefaultNormal;
    ret.Roughness = DefaultRoughness;
    ret.Metalness = DefaultMetalness;
    ret.Transmission = 0.0f;

    ret.Eta = isHitFromInside ? 1.5f : (1.0f / 1.5f);

    return ret;
}

MaterialSample sampleMaterial(uint materialId, vec2 texCoords, vec4 derivatives, uint flags, bool isHitFromInside, bool flipNormalY)
{
    uint materialType;
    uint materialIndex = unpackMaterialId(materialId, materialType);

    MaterialSample ret;
    if (materialType == MaterialTypeMetallicRoughness)
        ret = sampleMaterial(metallicRoughnessMaterials[materialIndex], texCoords, derivatives, isHitFromInside, flags);
    else
        ret = sampleMaterial(specularGlossinessMaterials[materialIndex], texCoords, derivatives, isHitFromInside, flags);

    if (flipNormalY)
        ret.Normal.y *= -1;

    return ret;
}
