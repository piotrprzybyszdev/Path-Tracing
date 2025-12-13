#include "ShaderRendererTypes.incl"
#include "Debug/DebugShaderTypes.incl"

#include "shading.glsl"

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

vec3 EvaluateReflection(vec3 V, vec3 L, vec3 F, float alpha, out float pdf)
{
    if (L.z <= 1e-5)
    {
        pdf = 0.0f;
        return vec3(0.0f, 0.0f, 0.0f);
    }

    alpha = max(alpha, 0.0001f);

    const vec3 H = normalize(V + L);

    const float LdotH = dot(L, H);
    const float VdotH = dot(V, H);

    const float D = GGXDistribution(H, alpha);

    const float GV = GGXSmith(V, alpha);
    const float GL = GGXSmith(L, alpha);
    const float G = GV * GL;

    pdf = (GV * max(VdotH, 0.0f) * D / V.z) / (4.0f * VdotH);
    return D * F * GV * GL / (4.0f * V.z);
}

vec3 SampleGGX(vec2 u, vec3 V, float alpha)
{
    vec3 Vh = normalize(vec3(alpha * V.x, alpha * V.y, abs(V.z)));

    const float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    const vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * (1.0f / sqrt(lensq)) : vec3(1, 0, 0);
    const vec3 T2 = cross(Vh, T1);

    const float r = sqrt(u.x);
    const float phi = 2.0 * PI * u.y;
    const float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    const float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    const vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    const vec3 Ne = normalize(vec3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));

    return Ne;
}