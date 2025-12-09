#include "common.glsl"
#include "shading.glsl"

struct BSDFSample
{
    vec3 Direction;
    float Pdf;
    vec3 Color;
};

vec3 evaluateDiffuseBRDF(MaterialSample material, vec3 V, vec3 L, out float pdf)
{
    pdf = L.z * 1.0f / PI;
    return L.z * material.Color / PI;
}

vec3 sampleDiffuseBRDF(MaterialSample material, vec2 u)
{
    return sampleCosineHemisphere(u);
}

vec3 evaluateGlossyBSDF(MaterialSample material, vec3 V, vec3 L, out float pdf)
{
    return EvaluateReflection(V, L, vec3(1.0f), material.Roughness * material.Roughness, pdf);
}

vec3 sampleGlossyBSDF(MaterialSample material, vec3 H, vec3 V)
{
    return normalize(reflect(-V, H));
}

vec3 evaluateMetallicBRDF(MaterialSample material, vec3 V, vec3 L, out float pdf)
{
    const vec3 H = normalize(V + L);
    const vec3 F0 = mix(material.Color, vec3(1.0f), SchlickFresnel(dot(V, H)));
    return EvaluateReflection(V, L, F0, material.Roughness * material.Roughness, pdf);
}

vec3 sampleMetallicBRDF(MaterialSample material, vec3 H, vec3 V)
{
    return normalize(reflect(-V, H));
}

vec3 evaluateBTDF(MaterialSample material, vec3 V, vec3 L, out float pdf)
{
    return EvaluateRefraction(V, L, material.Color, material.Roughness * material.Roughness, material.Eta, pdf);
}

vec3 sampleBTDF(MaterialSample material, vec3 H, vec3 V)
{
    return normalize(refract(-V, H, material.Eta));
}

struct LobePdfs
{
    float Diffuse;
    float Glossy;
    float Metallic;
    float Transmissive;
};

LobePdfs sampleLobePdfs(MaterialSample material, float F)
{
    LobePdfs pdfs;
    pdfs.Diffuse = (1.0f - material.Metalness) * (1.0f - F) * (1.0f - material.Transmission);
    pdfs.Glossy = (1.0f - material.Metalness) * F;
    pdfs.Metallic = material.Metalness;
    pdfs.Transmissive = (1.0f - material.Metalness) * (1.0f - F) * material.Transmission;
    return pdfs;
}

vec3 evaluateBSDF(MaterialSample material, vec3 V, vec3 L, out float outPdf)
{
    const bool isReflection = L.z > 0.0f;

    vec3 H = isReflection ? normalize(V + L) : normalize(material.Eta * V + L);
    const float FD = DielectricFresnel(abs(dot(V, H)), material.Eta);

    LobePdfs pdfs = sampleLobePdfs(material, FD);

    vec3 bsdf = vec3(0.0f);
    outPdf = 0.0f;
    float pdf;

    if (isReflection)
    {
        bsdf += evaluateDiffuseBRDF(material, V, L, pdf) * pdfs.Diffuse;
        outPdf += pdf * pdfs.Diffuse;

        bsdf += evaluateGlossyBSDF(material, V, L, pdf) * pdfs.Glossy;
        outPdf += pdf * pdfs.Glossy;
    
        bsdf += evaluateMetallicBRDF(material, V, L, pdf) * pdfs.Metallic;
        outPdf += pdf * pdfs.Metallic;
    }
    else
    {
        bsdf += evaluateBTDF(material, V, L, pdf) * pdfs.Transmissive;
        outPdf += pdf * pdfs.Transmissive;
    }

    return bsdf;
}

BSDFSample sampleBSDF(MaterialSample material, vec3 V, inout uint rngState)
{
    const float alpha = material.Roughness * material.Roughness;
    const vec3 H = SampleGGX(vec2(rand(rngState), rand(rngState)), V, alpha);
    const float FD = DielectricFresnel(abs(dot(V, H)), material.Eta);

    vec3 L;
    if (rand(rngState) < material.Metalness)
        L = sampleMetallicBRDF(material, H, V);
    else
    {
        if (rand(rngState) < FD)
            L = sampleGlossyBSDF(material, H, V);
        else
        {
            if (rand(rngState) < material.Transmission)
                L = sampleBTDF(material, H, V);
            else
                L = sampleDiffuseBRDF(material, vec2(rand(rngState), rand(rngState)));
        }
    }

    BSDFSample ret;
    ret.Direction = L;
    ret.Color = evaluateBSDF(material, V, L, ret.Pdf);

    return ret;
}
