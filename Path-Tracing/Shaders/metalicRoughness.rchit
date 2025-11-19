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

layout(binding = 6, set = 0) readonly buffer MaterialBuffer {
    MetalicRoughnessMaterial[] materials;
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
    const float lod = 0.0f;

    uint materialType;
    uint materialIndex = unpackMaterialId(sbt.MaterialId, materialType);
    const MetalicRoughnessMaterial material = materials[materialIndex];
    const vec3 normal = textureLod(textures[material.NormalIdx], vertex.TexCoords, lod).xyz;

    const mat3 TBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
    const vec3 N = normalize(vertex.Normal + TBN * (2.0f * normal - 1.0f));

    payload.Position = vertex.Position;
    payload.Normal = N;
    payload.MaterialId = sbt.MaterialId;
    payload.TexCoords = vertex.TexCoords;
    payload.HitDistance = gl_RayTmaxEXT;
    payload.Lod = lod;
}
