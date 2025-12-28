#include "ShaderRendererTypes.incl"
#include "Debug/DebugShaderTypes.incl"

#include "shading.glsl"

uint SampleValue(uint flags, uint disableFlag, uint value, uint defaultValue)
{
    if ((flags & disableFlag) == 0)
        return value;
    return defaultValue;
}

float SampleValue(uint flags, uint disableFlag, float value, float defaultValue)
{
    if ((flags & disableFlag) == 0)
        return value;
    return defaultValue;
}

vec3 SampleValue(uint flags, uint disableFlag, vec3 value, vec3 defaultValue)
{
    if ((flags & disableFlag) == 0)
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

void ComputeDpnDuv(Vertex v0, Vertex v1, Vertex v2, Vertex vertex, out vec3 dpdu, out vec3 dpdv, out vec3 dndu, out vec3 dndv)
{
    vec3 e1 = v1.Position - v0.Position;
    vec3 e2 = v2.Position - v0.Position;
    vec3 en1 = v1.Normal - v0.Normal;
    vec3 en2 = v2.Normal - v0.Normal;
    vec2 duv1 = v1.TexCoords - v0.TexCoords;
    vec2 duv2 = v2.TexCoords - v0.TexCoords;

    float det = duv1.x * duv2.y - duv2.x * duv1.y;

    if (abs(det) < 1e-8)
    {
        dpdu = vertex.Tangent;
        dpdv = vertex.Bitangent;
        dndu = vec3(0.0f);
        dndv = vec3(0.0f);
    }
    else
    {
        float invDet = 1.0f / det;
        dpdu = (duv2.y * e1 - duv1.y * e2) * invDet;
        dpdv = (-duv2.x * e1 + duv1.x * e2) * invDet;
        dndu = (duv2.y * en1 - duv1.y * en2) * invDet;
        dndv = (-duv2.x * en1 + duv1.x * en2) * invDet;
    }
}

void ComputeDpDxy(vec3 p, vec3 origin, vec3 direction, vec3 rxOrigin, vec3 rxDirection, vec3 ryOrigin, vec3 ryDirection, vec3 n, out vec3 dpdx, out vec3 dpdy)
{
    float d = -dot(n, p);
    float tx = (-dot(n, rxOrigin) - d) / dot(n, rxDirection);
    vec3 px = rxOrigin + tx * rxDirection;
    float ty = (-dot(n, ryOrigin) - d) / dot(n, ryDirection);
    vec3 py = ryOrigin + ty * ryDirection;

    dpdx = px - p;
    dpdy = py - p;
}

float DifferenceOfProducts(float a, float b, float c, float d)
{
    float cd = c * d;
    float differenceOfProducts = fma(a, b, -cd);
    float error = fma(-c, d, cd);
    return differenceOfProducts + error;
}

vec4 ComputeDerivatives(vec3 dpdx, vec3 dpdy, vec3 dpdu, vec3 dpdv)
{
    float ata00 = dot(dpdu, dpdu);
    float ata01 = dot(dpdu, dpdv);
    float ata11 = dot(dpdv, dpdv);

    float invDet = 1 / DifferenceOfProducts(ata00, ata11, ata01, ata01);
    invDet = isinf(invDet) ? 0.0f : invDet;

    float atb0x = dot(dpdu, dpdx);
    float atb1x = dot(dpdv, dpdx);
    float atb0y = dot(dpdu, dpdy);
    float atb1y = dot(dpdv, dpdy);

    float dudx = DifferenceOfProducts(ata11, atb0x, ata01, atb1x) * invDet;
    float dvdx = DifferenceOfProducts(ata00, atb1x, ata01, atb0x) * invDet;
    float dudy = DifferenceOfProducts(ata11, atb0y, ata01, atb1y) * invDet;
    float dvdy = DifferenceOfProducts(ata00, atb1y, ata01, atb0y) * invDet;

    dudx = isinf(dudx) ? 0.0f : clamp(dudx, -1e8f, 1e8f);
    dvdx = isinf(dvdx) ? 0.0f : clamp(dvdx, -1e8f, 1e8f);
    dudy = isinf(dudy) ? 0.0f : clamp(dudy, -1e8f, 1e8f);
    dvdy = isinf(dvdy) ? 0.0f : clamp(dvdy, -1e8f, 1e8f);

    return vec4(dudx, dvdx, dudy, dvdy);
}

void ComputeReflectedDifferentialRays(vec4 derivatives, vec3 n, vec3 p, vec3 viewDir, vec3 reflectedDir, vec3 dndu, vec3 dndv, inout vec3 rxOrigin, inout vec3 rxDirection, inout vec3 ryOrigin, inout vec3 ryDirection)
{
    float dudx = derivatives.x;
    float dudy = derivatives.y;
    float dvdx = derivatives.z;
    float dvdy = derivatives.w;

    vec3 dndx = dndu * dudx + dndv * dvdx;
    vec3 dndy = dndu * dudy + dndv * dvdy;

    float d = -dot(n, p);
    float tx = (-dot(n, rxOrigin) - d) / dot(n, rxDirection);
    vec3 px = rxOrigin + tx * rxDirection;
    float ty = (-dot(n, ryOrigin) - d) / dot(n, ryDirection);
    vec3 py = ryOrigin + ty * ryDirection;

    vec3 dwodx = -rxDirection - viewDir;
    vec3 dwody = -ryDirection - viewDir;

    rxOrigin = px;
    ryOrigin = py;

    float dwoDotn_dx = dot(dwodx, n) + dot(viewDir, dndx);
    float dwoDotn_dy = dot(dwody, n) + dot(viewDir, dndy);

    rxDirection = reflectedDir - dwodx + 2 * (dot(viewDir, n) * dndx + dwoDotn_dx * n);
    ryDirection = reflectedDir - dwody + 2 * (dot(viewDir, n) * dndy + dwoDotn_dy * n);
}

MaterialSample SampleMaterial(MetallicRoughnessMaterial material, vec2 texCoords, vec4 derivatives, uint flags)
{
    MaterialSample ret;

    vec2 dpdx = derivatives.xy;
    vec2 dpdy = derivatives.zw;

    uint colorIdx = SampleValue(flags, HitGroupFlagsDisableColorTexture, material.ColorIdx, DefaultColorTextureIndex);
    uint normalIdx = SampleValue(flags, HitGroupFlagsDisableNormalTexture, material.NormalIdx, DefaultNormalTextureIndex);
    uint roughnessIdx = SampleValue(flags, HitGroupFlagsDisableRoughnessTexture, material.RoughnessIdx, DefaultRoughnessTextureIndex);
    uint metallicIdx = SampleValue(flags, HitGroupFlagsDisableMetallicTexture, material.MetallicIdx, DefaultMetallicTextureIndex);

    ret.EmissiveColor = (textureGrad(textures[material.EmissiveIdx], texCoords, dpdx, dpdy).rgb + material.EmissiveColor) * material.EmissiveIntensity;
    ret.Color = textureGrad(textures[colorIdx], texCoords, dpdx, dpdy).rgb * material.Color.rgb;
    ret.Normal = ReconstructNormalFromXY(textureGrad(textures[normalIdx], texCoords, dpdx, dpdy).rgb);
    ret.Roughness = textureGrad(textures[roughnessIdx], texCoords, dpdx, dpdy).g * material.Roughness;
    ret.Metalness = textureGrad(textures[metallicIdx], texCoords, dpdx, dpdy).b * material.Metalness;

    return ret;
}

MaterialSample SampleMaterial(SpecularGlossinessMaterial material, vec2 texCoords, vec4 derivatives, uint flags)
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

MaterialSample SampleMaterial(uint materialId, vec2 texCoords, vec4 derivatives, uint flags, bool flipNormalY)
{
    uint materialType;
    uint materialIndex = unpackMaterialId(materialId, materialType);

    MaterialSample ret;
    if (materialType == MaterialTypeMetallicRoughness)
        ret = SampleMaterial(metallicRoughnessMaterials[materialIndex], texCoords, derivatives, flags);
    else
        ret = SampleMaterial(specularGlossinessMaterials[materialIndex], texCoords, derivatives, flags);

    if (flipNormalY)
        ret.Normal.y *= -1;

    return ret;
}

vec3 EvaluateReflection(vec3 V, vec3 L, vec3 F, float alpha, out float pdf)
{
    if (L.z < 0.00001f)
    {
        pdf = 0.0f;
        return vec3(0.0f, 0.0f, 0.0f);
    }

    const vec3 H = normalize(V + L);

    const float LdotH = dot(L, H);
    const float VdotH = dot(V, H);

    const float D = GGXDistribution(H, alpha);

    const float GV = GGXSmith(V, alpha);
    const float GL = GGXSmith(L, alpha);
    const float G = GV * GL;

    pdf = (GV * max(VdotH, 0.0f) * D / V.z) / (4.0f * VdotH);
    return G * D * F / (4.0f * V.z);
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

    return normalize(vec3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));
}

float ComputeLod(vec4 derivatives)
{
    float dudx = derivatives.x;
    float dvdx = derivatives.y;
    float dudy = derivatives.z;
    float dvdy = derivatives.w;
    float sx = sqrt(dudx * dudx + dvdx * dvdx);
    float sy = sqrt(dudy * dudy + dvdy * dvdy);
    float smax = max(sx, sy);
    return smax == 0.0f ? 0.0f : log2(smax);
}
