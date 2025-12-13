#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderRendererTypes.incl"
#include "DebugShaderTypes.incl"

layout(constant_id = DebugRenderModeConstantId) const uint s_RenderMode = RenderModeColor;
layout(constant_id = DebugHitGroupFlagsConstantId) const uint s_HitGroupFlags = HitGroupFlagsNone;

layout(binding = 0, set = 0) uniform accelerationStructureEXT u_TopLevelAS;

layout(binding = 3, set = 0) uniform sampler2D textures[];

layout(binding = 4, set = 0) readonly buffer TransformBuffer {
    mat3x4[] transforms;
};

layout(binding = 5, set = 0) readonly buffer GeometryBuffer {
    Geometry[] geometries;
};

layout(binding = 6, set = 0) readonly buffer MetalicRoughnessMaterialBuffer {
    MetalicRoughnessMaterial[] metalicRoughnessMaterials;
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

layout(location = 0) rayPayloadInEXT vec3 hitValue;
layout(location = 1) rayPayloadEXT bool isOccluded;
hitAttributeEXT vec3 attribs;

#include "common.glsl"
#include "sampling.glsl"
#include "shading.glsl"
#include "tracing.glsl"

const float ambient = 0.05f;

float DDDDistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0001);
}

float DDDGeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float DDDGeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = DDDGeometrySchlickGGX(NdotV, roughness);
    float ggx1 = DDDGeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 DDDfresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 DDDcomputeLightContribution(vec3 lightDir, vec3 lightColor, float attenuation, vec3 position, vec3 V, vec3 N, vec3 color, float roughness, float metalness)
{
    const vec3 L = -normalize(lightDir);

    const vec3 H = normalize(V + L);

    const vec3 R = 2.0f * dot(L, N) * N - L;

    const vec3 radiance = lightColor * attenuation;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, color, metalness);

    float NDF = DDDDistributionGGX(N, H, roughness);
    float G = DDDGeometrySmith(N, V, L, roughness);
    vec3 F = DDDfresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    vec3 specular = numerator / max(denominator, 0.0001);

    vec3 kS = F;

    vec3 kD = vec3(1.0) - kS;

    kD *= 1.0 - metalness;

    float NdotL = max(dot(N, L), 0.0);

    return (kD * color / PI + specular) * radiance * NdotL;
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

Vertex transform(Vertex vertex, uint transformIndex)
{
    const mat3x4 transform = mat3x4(mat4(transforms[transformIndex]) * gl_ObjectToWorld3x4EXT);

    vertex.Position = vec4(vertex.Position, 1.0f) * transform;
    vertex.Tangent = normalize(vec4(vertex.Tangent, 0.0f) * transform);
    vertex.Bitangent = normalize(vec4(vertex.Bitangent, 0.0f) * transform);
    vertex.Normal = normalize((vec4(vertex.Normal, 0.0f) * transpose(inverse(mat4(transform)))).xyz);  // TODO: Calculate inverse on the CPU

    return vertex;
}

void main()
{
    const vec3 barycentricCoords = computeBarycentricCoords(attribs);

    VertexBuffer vertices = VertexBuffer(geometries[sbt.GeometryIndex].Vertices);
    IndexBuffer indices = IndexBuffer(geometries[sbt.GeometryIndex].Indices);

    const Vertex originalVertex = getInterpolatedVertex(vertices, indices, gl_PrimitiveID * 3, barycentricCoords);
    const Vertex vertex = transform(originalVertex, sbt.TransformIndex);

    // TODO: Calculate the LOD properly
    const float lod = (s_HitGroupFlags & HitGroupFlagsDisableMipMaps) != HitGroupFlagsNone ? 0.0f : log2(gl_RayTmaxEXT);

    MaterialSample material = SampleMaterial(sbt.MaterialId, vertex.TexCoords, lod, s_HitGroupFlags);

    const vec3 viewDir = gl_WorldRayDirectionEXT;
    const vec3 V = -normalize(viewDir);
    const mat3 TBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
    const vec3 N = normalize(vertex.Normal + TBN * (2.0f * material.Normal - 1.0f));

    vec3 totalLight = material.Color * ambient + material.EmissiveColor;

    bool shadowsDisabled = (s_HitGroupFlags & HitGroupFlagsDisableShadows) != HitGroupFlagsNone;

    if (shadowsDisabled || !checkOccluded(u_DirectionalLight.Direction, vertex.Position, DirectionalLightDistance))
        totalLight += DDDcomputeLightContribution(u_DirectionalLight.Direction, u_DirectionalLight.Color, 1.0f, vertex.Position, V, N, material.Color, material.Roughness, material.Metalness);
    
    for (uint lightIndex = 0; lightIndex < u_LightCount; lightIndex++)
    {
        const PointLight light = u_Lights[lightIndex];
        const vec3 lightDirection = vertex.Position - light.Position;
        const float dist = length(lightDirection);
        const float attenuation = 1.0f / (light.AttenuationConstant + dist * light.AttenuationLinear + dist * dist * light.AttenuationQuadratic);
        
        if (shadowsDisabled || !checkOccluded(lightDirection, vertex.Position, dist))
        {
            const vec3 lightContribution = DDDcomputeLightContribution(lightDirection, light.Color, attenuation, vertex.Position, V, N, material.Color, material.Roughness, material.Metalness);
            totalLight += lightContribution;
        }
    }
    
    switch (s_RenderMode)
    {
    case RenderModeColor:
        hitValue = totalLight;
        break;
    case RenderModeWorldPosition:
        hitValue = vertex.Position;
        break;
    case RenderModeNormal:
        hitValue = N;
        break;
    case RenderModeTextureCoords:
        hitValue = vec3(vertex.TexCoords, 0.0f);
        break;
    case RenderModeMips:
        hitValue = vec3(floor(lod) / 12.0f);
        break;
    case RenderModeGeometry:
        hitValue = getRandomColor(gl_GeometryIndexEXT);
        break;
    case RenderModePrimitive:
        hitValue = getRandomColor(gl_PrimitiveID);
        break;
    case RenderModeInstance:
        hitValue = getRandomColor(gl_InstanceID);
        break;
    }
}
