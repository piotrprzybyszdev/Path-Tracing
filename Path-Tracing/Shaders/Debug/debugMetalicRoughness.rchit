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

layout(binding = 6, set = 0) readonly buffer MaterialBuffer {
    MetalicRoughnessMaterial[] materials;
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
#include "shading.glsl"
#include "tracing.glsl"
#include "debug.glsl"

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

    const MetalicRoughnessMaterial material = materials[sbt.MaterialIndex];
    const vec3 color = textureLod(textures[GetColorTextureIndex(s_HitGroupFlags, material)], vertex.TexCoords, lod).xyz * material.Color;
    const vec3 normal = textureLod(textures[GetNormalTextureIndex(s_HitGroupFlags, material)], vertex.TexCoords, lod).xyz;
    const float roughness = textureLod(textures[GetRoughnessTextureIndex(s_HitGroupFlags, material)], vertex.TexCoords, lod).y * material.Roughness;
    const float metalness = textureLod(textures[GetMetalicTextureIndex(s_HitGroupFlags, material)], vertex.TexCoords, lod).z * material.Metalness;

    const vec3 viewDir = gl_WorldRayDirectionEXT;
    const vec3 V = -normalize(viewDir);
    const mat3 TBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
    const vec3 N = normalize(vertex.Normal + TBN * (2.0f * normal - 1.0f));

    vec3 totalLight = color * ambient;

    bool shadowsDisabled = (s_HitGroupFlags & HitGroupFlagsDisableShadows) != HitGroupFlagsNone;

    if (shadowsDisabled || !checkOccluded(u_DirectionalLight.Direction, vertex.Position, DirectionalLightDistance))
        totalLight += computeLightContribution(u_DirectionalLight.Direction, u_DirectionalLight.Color, 1.0f, vertex.Position, V, N, color, roughness, metalness);
    
    for (uint lightIndex = 0; lightIndex < u_LightCount; lightIndex++)
    {
        const PointLight light = u_Lights[lightIndex];
        const vec3 lightDirection = vertex.Position - light.Position;
        const float dist = length(lightDirection);
        const float attenuation = 1.0f / (light.AttenuationConstant + dist * light.AttenuationLinear + dist * dist * light.AttenuationQuadratic);
        
        if (shadowsDisabled || !checkOccluded(lightDirection, vertex.Position, dist))
        {
            const vec3 lightContribution = computeLightContribution(lightDirection, light.Color, attenuation, vertex.Position, V, N, color, roughness, metalness);
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
        hitValue = vec3(floor(lod) / textureQueryLevels(textures[GetColorTextureIndex(s_HitGroupFlags, material)]));
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
