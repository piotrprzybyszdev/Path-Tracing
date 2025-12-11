#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderRendererTypes.incl"
#include "common.glsl"

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

layout(location = 0) rayPayloadInEXT Payload payload;
hitAttributeEXT vec3 attribs;

#include "shading.glsl"
#include "sampling.glsl"

Vertex transform(Vertex vertex, uint transformIndex)
{
    const mat3x4 transform = mat3x4(mat4(transforms[transformIndex]) * gl_ObjectToWorld3x4EXT);

    vertex.Position = vec4(vertex.Position, 1.0f) * transform;
    vertex.Tangent = normalize(vec4(vertex.Tangent, 0.0f) * transform);
    vertex.Bitangent = normalize(vec4(vertex.Bitangent, 0.0f) * transform);
    vertex.Normal = normalize((vec4(vertex.Normal, 0.0f) * transpose(inverse(mat4(transform)))).xyz);  // TODO: Calculate inverse on the CPU

    return vertex;
}

vec2 SampleUniformDiskConcentric(vec2 u, out float theta)
{
    vec2 offset = 2.0f * u - 1.0f;
    if (offset == vec2(0.0f))
        return vec2(0.0f);

    if (abs(offset.x) > abs(offset.y))
    {
        theta = PI / 4 * (offset.y / offset.x);
        return offset.x * vec2(cos(theta), sin(theta));
    }
    else
    {
        theta = PI / 2 - PI / 4 * (offset.x / offset.y);
        return offset.y * vec2(cos(theta), sin(theta));
    }
}

vec3 SampleCosineHemisphere(vec2 u, out float pdf)
{
    float theta;
    vec2 d = SampleUniformDiskConcentric(u, theta);
    float z = sqrt(1 - d.x * d.x - d.y * d.y);
    pdf = abs(cos(theta)) / PI;
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
        ret.Color = u_DirectionalLight.Color;
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

BSDFSample SampleDiffuseBRDF(vec2 u, vec3 color)
{
    BSDFSample ret;

    ret.Direction = SampleCosineHemisphere(u, ret.Pdf);
    ret.Color = color / PI;

    return ret;
}

vec3 EvaluateDiffuseBRDF(vec3 color)
{
    return color / PI;
}

void main()
{
    const vec3 barycentricCoords = computeBarycentricCoords(attribs);

    VertexBuffer vertices = VertexBuffer(geometries[sbt.GeometryIndex].Vertices);
    IndexBuffer indices = IndexBuffer(geometries[sbt.GeometryIndex].Indices);

    const Vertex originalVertex = getInterpolatedVertex(vertices, indices, gl_PrimitiveID * 3, barycentricCoords);
    const Vertex vertex = transform(originalVertex, sbt.TransformIndex);

    // TODO: Calculate the LOD properly
    const float lod = 0.0f;

    MaterialSample material = SampleMaterial(sbt.MaterialId, vertex.TexCoords, lod, 0);
    const vec3 normal = material.Normal;

    const mat3 TBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
    const vec3 N = normalize(vertex.Normal + TBN * normal);

    uint rngState = payload.RngState;
    
    float lightPdf;
    BSDFSample bsdf = SampleDiffuseBRDF(vec2(rand(rngState), rand(rngState)), material.Color);
    LightSample light = SampleLight(rand(rngState), vertex.Position, lightPdf);

    const vec3 directLight = light.Color * light.Attenuation;
    const vec3 dlbrdf = EvaluateDiffuseBRDF(material.Color) * abs(dot(N, light.Direction));

    payload.Position = vertex.Position + vertex.Normal * 0.001f;
    payload.Direction = normalize(TBN * bsdf.Direction);
    payload.Bsdf = bsdf.Color;
    payload.Pdf = bsdf.Pdf * abs(dot(N, payload.Direction));
    payload.Emissive = material.EmissiveColor;
    payload.RngState = rngState;
    payload.DirectLight = directLight * dlbrdf;
    payload.DirectLightPdf = lightPdf;
    payload.LightDirection = light.Direction;
    payload.LightDistance = light.Distance;
}
