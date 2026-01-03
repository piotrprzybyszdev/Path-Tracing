#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderRendererTypes.incl"
#include "common.glsl"
#include "shading.glsl"

layout(constant_id = HitFlagsConstantId) const uint s_HitFlags = HitFlagsNone;

layout(binding = 3, set = 0) uniform sampler2D textures[];

layout(binding = 4, set = 0) readonly buffer TransformBuffer {
    mat3x4[] transforms;
};

layout(binding = 5, set = 0) readonly buffer GeometryBuffer {
    Geometry[] geometries;
};

layout(binding = 6, set = 0) readonly buffer MetallicRoughnessMaterialBuffer {
    MetallicRoughnessMaterial[] metallicRoughnessMaterials;
};

layout(binding = 7, set = 0) readonly buffer SpecularGlossinessMaterialBuffer {
    SpecularGlossinessMaterial[] specularGlossinessMaterials;
};

layout(binding = 8, set = 0) uniform LightsBuffer {
    uint u_LightCount;
    DirectionalLight u_DirectionalLight;
    PointLight[MaxLightCount] u_Lights;
};

layout(shaderRecordEXT, std430) buffer SBT {
    SBTBuffer sbt;
};

layout(location = 0) rayPayloadInEXT Payload payload;
hitAttributeEXT vec3 attribs;

#include "shading.glsl"
#include "sampling.glsl"

mat3 ComputeTangentSpace(vec3 normal)
{
    vec3 t1 = cross(normal, vec3(1.0f, 0.0f, 0.0f));
    vec3 t2 = cross(normal, vec3(0.0f, 1.0f, 0.0f));

    vec3 tangent = length(t1) > length(t2) ? t1 : t2;
    vec3 bitangent = cross(normal, tangent);
    
    return mat3(normalize(tangent), normalize(bitangent), normal);
};

Vertex transform(Vertex vertex, uint transformIndex)
{
    const mat3x4 transform = mat3x4(mat4(transforms[transformIndex]) * gl_ObjectToWorld3x4EXT);

    vertex.Position = vec4(vertex.Position, 1.0f) * transform;
    vertex.Tangent = normalize(vec4(vertex.Tangent, 0.0f) * transform);
    vertex.Bitangent = normalize(vec4(vertex.Bitangent, 0.0f) * transform);
    vertex.Normal = normalize((vec4(vertex.Normal, 0.0f) * transpose(inverse(mat4(transform)))).xyz);  // TODO: Calculate inverse on the CPU

    return vertex;
}

vec3 SampleCosineHemisphere(vec2 u)
{
    vec2 d = SampleUniformDiskConcentric(u);
    float z = sqrt(1 - d.x * d.x - d.y * d.y);
    return vec3(d, z);
}

struct LightSample
{
    vec3 Direction;
    float Distance;
    vec3 Color;
    float Attenuation;
};

LightSample SampleLight(float u, vec3 position, out float pdf)
{
    uint lightIndex = uint(u * (u_LightCount + 1));
    pdf = 1.0f / (u_LightCount + 1);
    LightSample ret;

    if (lightIndex >= u_LightCount)
    {
        ret.Direction = normalize(u_DirectionalLight.Direction);
        // TODO: Proper sampling
        ret.Color = hdrToLdr(u_DirectionalLight.Color);
        ret.Distance = DirectionalLightDistance;
        ret.Attenuation = 1.0f;
        return ret;
    }

    const PointLight light = u_Lights[lightIndex];

    ret.Distance = distance(position, light.Position);
    ret.Direction = normalize(position - light.Position);
    ret.Color = light.Color;
    const float attenuation = 1.0f / (light.AttenuationConstant + ret.Distance * light.AttenuationLinear + ret.Distance * ret.Distance * light.AttenuationQuadratic);
    ret.Attenuation = clamp(attenuation, 0.0f, 1.0f);
    
    return ret;
}

struct BSDFSample
{
    vec3 Direction;
    float Pdf;
    vec3 Color;
};

vec3 EvaluateDiffuseBRDF(MaterialSample material, vec3 V, vec3 L, out float pdf)
{
    pdf = L.z * 1.0f / PI;
    return L.z * material.Color / PI;
}

vec3 SampleDiffuseBRDF(MaterialSample material, vec2 u)
{
    return SampleCosineHemisphere(u);
}

vec3 EvaluateGlossyBSDF(MaterialSample material, vec3 V, vec3 L, out float pdf)
{
    return EvaluateReflection(V, L, vec3(1.0f), material.Roughness * material.Roughness, pdf);
}

vec3 SampleGlossyBSDF(MaterialSample material, vec3 H, vec3 V)
{
    return normalize(reflect(-V, H));
}

vec3 EvaluateMetallicBRDF(MaterialSample material, vec3 V, vec3 L, out float pdf)
{
    const vec3 H = normalize(V + L);
    const vec3 F0 = mix(material.Color, vec3(1.0f), SchlickFresnel(dot(V, H)));
    return EvaluateReflection(V, L, F0, material.Roughness * material.Roughness, pdf);
}

vec3 SampleMetallicBRDF(MaterialSample material, vec3 H, vec3 V)
{
    return normalize(reflect(-V, H));
}

vec3 EvaluateBTDF(MaterialSample material, vec3 V, vec3 L, out float pdf)
{
    return EvaluateRefraction(V, L, material.Color, material.Roughness * material.Roughness, material.Eta, pdf);
}

vec3 SampleBTDF(MaterialSample material, vec3 H, vec3 V)
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

LobePdfs SampleLobePdfs(MaterialSample material, float F)
{
    LobePdfs pdfs;
    pdfs.Diffuse = (1.0f - material.Metalness) * (1.0f - F) * (1.0f - material.Transmission);
    pdfs.Glossy = (1.0f - material.Metalness) * F;
    pdfs.Metallic = material.Metalness;
    pdfs.Transmissive = (1.0f - material.Metalness) * (1.0f - F) * material.Transmission;
    return pdfs;
}

vec3 EvaluateBSDF(MaterialSample material, vec3 V, vec3 L, out float outPdf)
{
    const bool isReflection = L.z > 0.0f;

    vec3 H = isReflection ? normalize(V + L) : normalize(material.Eta * V + L);
    const float FD = DielectricFresnel(abs(dot(V, H)), material.Eta);

    LobePdfs pdfs = SampleLobePdfs(material, FD);

    vec3 bsdf = vec3(0.0f);
    outPdf = 0.0f;
    float pdf;

    if (isReflection)
    {
        bsdf += EvaluateDiffuseBRDF(material, V, L, pdf) * pdfs.Diffuse;
        outPdf += pdf * pdfs.Diffuse;

        bsdf += EvaluateGlossyBSDF(material, V, L, pdf) * pdfs.Glossy;
        outPdf += pdf * pdfs.Glossy;
    
        bsdf += EvaluateMetallicBRDF(material, V, L, pdf) * pdfs.Metallic;
        outPdf += pdf * pdfs.Metallic;
    }
    else
    {
        bsdf += EvaluateBTDF(material, V, L, pdf) * pdfs.Transmissive;
        outPdf += pdf * pdfs.Transmissive;
    }

    return bsdf;
}

BSDFSample SampleBSDF(MaterialSample material, vec3 V, inout uint rngState)
{
    const float alpha = material.Roughness * material.Roughness;
    const vec3 H = SampleGGX(vec2(rand(rngState), rand(rngState)), V, alpha);
    const float FD = DielectricFresnel(abs(dot(V, H)), material.Eta);

    vec3 L;
    if (rand(rngState) < material.Metalness)
        L = SampleMetallicBRDF(material, H, V);
    else
    {
        if (rand(rngState) < FD)
            L = SampleGlossyBSDF(material, H, V);
        else
        {
            if (rand(rngState) < material.Transmission)
                L = SampleBTDF(material, H, V);
            else
                L = SampleDiffuseBRDF(material, vec2(rand(rngState), rand(rngState)));
        }
    }

    BSDFSample ret;
    ret.Direction = L;
    ret.Color = EvaluateBSDF(material, V, L, ret.Pdf);

    return ret;
}

void main()
{
    const vec3 barycentricCoords = computeBarycentricCoords(attribs);

    VertexBuffer vertices = VertexBuffer(geometries[sbt.GeometryIndex].Vertices);
    IndexBuffer indices = IndexBuffer(geometries[sbt.GeometryIndex].Indices);

    const Vertex originalVertex = getInterpolatedVertex(vertices, indices, gl_PrimitiveID * 3, barycentricCoords);
    Vertex vertex = transform(originalVertex, sbt.TransformIndex);

    const bool isHitFromInside = dot(vertex.Normal, gl_WorldRayDirectionEXT) > 0.0f;
    if (isHitFromInside)
    {
        vertex.Normal *= -1;
        vertex.Tangent *= -1;
        vertex.Bitangent *= -1;
    }

    const vec3 origin = gl_WorldRayOriginEXT;
    const vec3 viewDir = gl_WorldRayDirectionEXT;

    // Calculate geometric dP/du and dP/dv
    Vertex v0 = getVertex(vertices, indices, gl_PrimitiveID * 3);
    Vertex v1 = getVertex(vertices, indices, gl_PrimitiveID * 3 + 1);
    Vertex v2 = getVertex(vertices, indices, gl_PrimitiveID * 3 + 2);

    v0 = transform(v0, sbt.TransformIndex);
    v1 = transform(v1, sbt.TransformIndex);
    v2 = transform(v2, sbt.TransformIndex);

    vec3 dpdu, dpdv, dndu, dndv;
    ComputeDpnDuv(v0, v1, v2, vertex, dpdu, dpdv, dndu, dndv);

    vec3 rxOrigin = payload.RxOrigin;
    vec3 rxDirection = payload.RxDirection;
    vec3 ryOrigin = payload.RyOrigin;
    vec3 ryDirection = payload.RyDirection;

    vec3 dpdx, dpdy;
    ComputeDpDxy(vertex.Position, origin, normalize(viewDir), rxOrigin, rxDirection, ryOrigin, ryDirection, vertex.Normal, dpdx, dpdy);

    const vec4 derivatives = ComputeDerivatives(dpdx, dpdy, dpdu, dpdv);

    const bool flipYNormal = (s_HitFlags & HitFlagsDxNormalTextures) != HitFlagsNone;
    MaterialSample material = SampleMaterial(sbt.MaterialId, vertex.TexCoords, derivatives, 0, isHitFromInside, flipYNormal);
    
    // Handling decals
    if (payload.DirectLightPdf != -1.0f && gl_RayTmaxEXT > payload.DirectLightPdf)
        material.Color = mix(material.Color, payload.LightDirection.rgb, payload.LightDistance);
    
    // Prevents firefly artifacts
    payload.MaxRoughness = max(material.Roughness, payload.MaxRoughness);

    // Glossy lobe is numerically unstable at very low roughness
    material.Roughness = max(payload.MaxRoughness, 0.01f);  // TODO: Consider adding a perfect specular lobe
    
    const mat3 geometryTBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
    const vec3 N = normalize(vertex.Normal + geometryTBN * material.Normal);
    const mat3 TBN = ComputeTangentSpace(N);
    const vec3 V = normalize(inverse(TBN) * normalize(-gl_WorldRayDirectionEXT));
    
    uint rngState = payload.RngState;

    float lightPdf, lightSmplPdf;  // unused
    LightSample light = SampleLight(rand(rngState), vertex.Position, lightPdf);
    const vec3 L = normalize(inverse(TBN) * -light.Direction);
    const vec3 lightBsdf = EvaluateBSDF(material, V, L, lightSmplPdf);

    BSDFSample bsdf = SampleBSDF(material, V, rngState);

    payload.Direction = normalize(TBN * bsdf.Direction);
    payload.Position = vertex.Position + payload.Direction * 0.001f;  // TODO: Do we prefer offsetting by the normal?
    payload.Bsdf = bsdf.Color;
    payload.Pdf = bsdf.Pdf;
    payload.Emissive = material.EmissiveColor;
    payload.RngState = rngState;
    payload.DirectLight = light.Color * light.Attenuation * lightBsdf;
    payload.DirectLightPdf = lightPdf;
    payload.LightDirection = light.Direction;
    payload.LightDistance = light.Distance;

    ComputeReflectedDifferentialRays(derivatives, vertex.Normal, vertex.Position, viewDir, payload.Direction, dndu, dndv, rxOrigin, rxDirection, ryOrigin, ryDirection);
    payload.RxOrigin = rxOrigin;
    payload.RxDirection = rxDirection;
    payload.RyOrigin = ryOrigin;
    payload.RyDirection = ryDirection;
}
