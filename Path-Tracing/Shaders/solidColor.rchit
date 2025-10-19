#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderRendererTypes.incl"
#include "common.glsl"

layout(constant_id = RenderModeConstantId) const uint s_RenderMode = RenderModeColor;
layout(constant_id = HitGroupFlagsConstantId) const uint s_HitGroupFlags = HitGroupFlagsNone;

layout(binding = 0, set = 0) uniform accelerationStructureEXT u_TopLevelAS;

layout(binding = 4, set = 0) readonly buffer TransformBuffer {
    mat3x4[] transforms;
};

layout(binding = 5, set = 0) readonly buffer GeometryBuffer {
    Geometry[] geometries;
};

layout(binding = 7, set = 0) readonly buffer MaterialBuffer {
    SolidColorMaterial[] materials;
};

layout(shaderRecordEXT, std430) buffer SBT {
    SBTBuffer sbt;
};

layout(location = 0) rayPayloadInEXT Payload payload;
hitAttributeEXT vec3 attribs;

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
    const float lod = 0.0f; //log2(gl_RayTmaxEXT);

    const SolidColorMaterial material = materials[sbt.MaterialIndex];
    const vec3 N = normalize(vertex.Normal);

    payload.Position = vertex.Position;
    payload.Normal = N;
    payload.MaterialId = packMaterialId(sbt.MaterialIndex, MaterialTypeSolidColor);
    payload.TexCoords = vertex.TexCoords;
    payload.HitDistance = gl_RayTmaxEXT;
    payload.Lod = lod;
}
