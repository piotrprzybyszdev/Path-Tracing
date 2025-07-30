#include "ShaderRendererTypes.incl"

vec3 computeBarycentricCoords(vec3 attribs)
{
    return vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
}

Vertex getVertex(VertexBuffer vertices, IndexBuffer indices, uint offset)
{
    const uint index = indices.i[offset];
    const vec2 p1 = vertices.v[index * 7];
    const vec2 p2 = vertices.v[index * 7 + 1];
    const vec2 p3 = vertices.v[index * 7 + 2];
    const vec2 p4 = vertices.v[index * 7 + 3];
    const vec2 p5 = vertices.v[index * 7 + 4];
    const vec2 p6 = vertices.v[index * 7 + 5];
    const vec2 p7 = vertices.v[index * 7 + 6];

    Vertex v;
    v.Position = vec3(p1, p2.x);
    v.TexCoords = vec2(p2.y, p3.x);
    v.Normal = vec3(p3.y, p4);
    v.Tangent = vec3(p5, p6.x);
    v.Bitangent = vec3(p6.y, p7);

    return v;
}

vec2 interpolate(vec2 v1, vec2 v2, vec2 v3, vec3 barycentricCoords)
{
    return v1 * barycentricCoords.x + v2 * barycentricCoords.y + v3 * barycentricCoords.z;
}

vec3 interpolate(vec3 v1, vec3 v2, vec3 v3, vec3 barycentricCoords)
{
    return v1 * barycentricCoords.x + v2 * barycentricCoords.y + v3 * barycentricCoords.z;
}

Vertex interpolate(Vertex v1, Vertex v2, Vertex v3, vec3 barycentricCoords)
{
    Vertex v;
    v.Position = interpolate(v1.Position, v2.Position, v3.Position, barycentricCoords).xyz;
    v.TexCoords = interpolate(v1.TexCoords, v2.TexCoords, v3.TexCoords, barycentricCoords);
    v.Normal = interpolate(v1.Normal, v2.Normal, v3.Normal, barycentricCoords);
    v.Tangent = interpolate(v1.Tangent, v2.Tangent, v3.Tangent, barycentricCoords);
    v.Bitangent = interpolate(v1.Bitangent, v2.Bitangent, v3.Bitangent, barycentricCoords);

    return v;
}

uint GetColorTextureIndex(uint enabledTextures, Material material)
{
    return (enabledTextures & TexturesEnableColor) != 0 ? material.ColorIdx : DefaultColorTextureIndex;
}

uint GetNormalTextureIndex(uint enabledTextures, Material material)
{
    return (enabledTextures & TexturesEnableNormal) != 0 ? material.NormalIdx : DefaultNormalTextureIndex;
}

uint GetRoughnessTextureIndex(uint enabledTextures, Material material)
{
    return (enabledTextures & TexturesEnableRoughness) != 0 ? material.RoughnessIdx : DefaultRoughnessTextureIndex;
}

uint GetMetalicTextureIndex(uint enabledTextures, Material material)
{
    return (enabledTextures & TexturesEnableMetalic) != 0 ? material.MetalicIdx : DefaultMetalicTextureIndex;
}

Vertex getInterpolatedVertex(VertexBuffer vertices, IndexBuffer indices, uint indexOffset, vec3 barycentricCoords)
{
    return interpolate(
        getVertex(vertices, indices, indexOffset), getVertex(vertices, indices, indexOffset + 1),
        getVertex(vertices, indices, indexOffset + 2), barycentricCoords
    );
}
