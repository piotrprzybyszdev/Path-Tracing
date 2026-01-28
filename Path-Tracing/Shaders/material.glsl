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

uint getColorTextureIdx(uint materialIndex, uint materialType)
{
    switch (materialType)
    {
        case MaterialTypeMetallicRoughness:
            return metallicRoughnessMaterials[materialIndex].ColorIdx;
        case MaterialTypeSpecularGlossiness:
            return specularGlossinessMaterials[materialIndex].ColorIdx;
        case MaterialTypePhong:
            return phongMaterials[materialIndex].ColorIdx;
        default:
            return 0;
    }
}

vec4 getColorFactor(uint materialIndex, uint materialType)
{
    switch (materialType)
    {
        case MaterialTypeMetallicRoughness:
            return metallicRoughnessMaterials[materialIndex].Color;
        case MaterialTypeSpecularGlossiness:
            return specularGlossinessMaterials[materialIndex].Color;
        case MaterialTypePhong:
            return phongMaterials[materialIndex].Color;
        default:
            return vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
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

    ret.EmissiveColor = (textureGrad(textures[material.EmissiveIdx], texCoords, dpdx, dpdy).rgb + material.EmissiveColor) * material.EmissiveIntensity;
    ret.Color = textureGrad(textures[colorIdx], texCoords, dpdx, dpdy).rgb * material.Color.rgb;
    ret.Normal = ReconstructNormalFromXY(textureGrad(textures[normalIdx], texCoords, dpdx, dpdy).rgb);
    ret.Roughness = textureGrad(textures[material.RoughnessIdx], texCoords, dpdx, dpdy).g * material.Roughness;
    ret.Metalness = textureGrad(textures[material.MetallicIdx], texCoords, dpdx, dpdy).b * material.Metalness;
    ret.Transmission = material.Transmission;
    ret.AttenuationColor = material.AttenuationColor;
    ret.AttenuationDistance = material.AttenuationDistance;

    ret.Eta = isHitFromInside ? material.Ior : (1.0f / material.Ior);

    return ret;
}

MaterialSample sampleMaterial(SpecularGlossinessMaterial material, vec2 texCoords, vec4 derivatives, bool isHitFromInside, uint flags)
{
    MaterialSample ret;

    vec2 dpdx = derivatives.xy;
    vec2 dpdy = derivatives.zw;

    uint colorIdx = sampleValue(flags, HitGroupFlagsDisableColorTexture, material.ColorIdx, DefaultColorTextureIndex);
    uint normalIdx = sampleValue(flags, HitGroupFlagsDisableNormalTexture, material.NormalIdx, DefaultNormalTextureIndex);
    
    ret.EmissiveColor = (textureGrad(textures[material.EmissiveIdx], texCoords, dpdx, dpdy).rgb + material.EmissiveColor) * material.EmissiveIntensity;
    ret.Color = textureGrad(textures[colorIdx], texCoords, dpdx, dpdy).rgb * material.Color.rgb;
    ret.Normal = ReconstructNormalFromXY(textureGrad(textures[normalIdx], texCoords, dpdx, dpdy).rgb);
    ret.Transmission = material.Transmission;
    ret.AttenuationColor = material.AttenuationColor;
    ret.AttenuationDistance = material.AttenuationDistance;

    ret.Eta = isHitFromInside ? material.Ior : (1.0f / material.Ior);

    vec3 specular = textureGrad(textures[material.SpecularIdx], texCoords, dpdx, dpdy).rgb * material.Specular;
    float glossiness = textureGrad(textures[material.GlossinessIdx], texCoords, dpdx, dpdy).a * material.Glossiness;

    ret.Roughness = 1.0f - glossiness;
    const vec3 diff = max(specular - 0.04f, 0.0f) / ((ret.Color - 0.04f) + 0.00001f);
    ret.Metalness = (diff.x + diff.y + diff.z) / 3.0f;

    return ret;
}

MaterialSample sampleMaterial(PhongMaterial material, vec2 texCoords, vec4 derivatives, bool isHitFromInside, uint flags)
{
    MaterialSample ret;

    vec2 dpdx = derivatives.xy;
    vec2 dpdy = derivatives.zw;

    uint colorIdx = sampleValue(flags, HitGroupFlagsDisableColorTexture, material.ColorIdx, DefaultColorTextureIndex);
    uint normalIdx = sampleValue(flags, HitGroupFlagsDisableNormalTexture, material.NormalIdx, DefaultNormalTextureIndex);

    ret.EmissiveColor = (textureGrad(textures[material.EmissiveIdx], texCoords, dpdx, dpdy).rgb + material.EmissiveColor) * material.EmissiveIntensity;
    ret.Color = textureGrad(textures[colorIdx], texCoords, dpdx, dpdy).rgb * material.Color.rgb;
    ret.Normal = ReconstructNormalFromXY(textureGrad(textures[normalIdx], texCoords, dpdx, dpdy).rgb);
    ret.Transmission = material.Transmission;
    ret.AttenuationColor = material.AttenuationColor;
    ret.AttenuationDistance = material.AttenuationDistance;

    ret.Eta = isHitFromInside ? material.Ior : (1.0f / material.Ior);

    vec3 specular = textureGrad(textures[material.SpecularIdx], texCoords, dpdx, dpdy).rgb * material.Specular;
    float shininess = textureGrad(textures[material.ShininessIdx], texCoords, dpdx, dpdy).a * material.Shininess;

    ret.Roughness = 1.0f - shininess;
    const vec3 diff = max(specular - 0.04f, 0.0f) / ((ret.Color - 0.04f) + 0.00001f);
    ret.Metalness = (diff.x + diff.y + diff.z) / 3.0f;

    return ret;
}

MaterialSample sampleMaterial(uint materialId, vec2 texCoords, vec4 derivatives, uint flags, bool isHitFromInside, bool flipNormalY)
{
    uint materialType;
    uint materialIndex = unpackMaterialId(materialId, materialType);

    MaterialSample ret;
    switch (materialType)
    {
        case MaterialTypeMetallicRoughness:
            ret = sampleMaterial(metallicRoughnessMaterials[materialIndex], texCoords, derivatives, isHitFromInside, flags);
            break;
        case MaterialTypeSpecularGlossiness:
            ret = sampleMaterial(specularGlossinessMaterials[materialIndex], texCoords, derivatives, isHitFromInside, flags);
            break;
        case MaterialTypePhong:
            ret = sampleMaterial(phongMaterials[materialIndex], texCoords, derivatives, isHitFromInside, flags);
            break;
        default:
            ret.Color = vec3(1.0f, 0.0f, 0.0f);
            ret.EmissiveColor = vec3(1.0f, 0.0f, 0.0f);
            break;
    }

    if (flipNormalY)
        ret.Normal.y *= -1;

    return ret;
}
