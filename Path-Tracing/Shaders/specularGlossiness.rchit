#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderRendererTypes.incl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT u_TopLevelAS;

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

#include "common.glsl"
#include "sampling.glsl"

void main()
{
    const vec3 barycentricCoords = computeBarycentricCoords(attribs);

    VertexBuffer vertices = VertexBuffer(geometries[sbt.GeometryIndex].Vertices);
    IndexBuffer indices = IndexBuffer(geometries[sbt.GeometryIndex].Indices);

    const Vertex originalVertex = getInterpolatedVertex(vertices, indices, gl_PrimitiveID * 3, barycentricCoords);
    const Vertex vertex = transform(originalVertex, sbt.TransformIndex);
}
