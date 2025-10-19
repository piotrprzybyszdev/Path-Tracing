#include "ShaderRendererTypes.incl"

const float PI = 3.14159265359f;

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

AnimatedVertex getAnimatedVertex(AnimatedVertexBuffer vertices, uint index)
{
    const vec2 p1 = vertices.v[index * 11];
    const vec2 p2 = vertices.v[index * 11 + 1];
    const vec2 p3 = vertices.v[index * 11 + 2];
    const vec2 p4 = vertices.v[index * 11 + 3];
    const vec2 p5 = vertices.v[index * 11 + 4];
    const vec2 p6 = vertices.v[index * 11 + 5];
    const vec2 p7 = vertices.v[index * 11 + 6];
    const vec2 p8 = vertices.v[index * 11 + 7];
    const vec2 p9 = vertices.v[index * 11 + 8];
    const vec2 p10 = vertices.v[index * 11 + 9];
    const vec2 p11 = vertices.v[index * 11 + 10];

    AnimatedVertex v;
    v.Position = vec3(p1, p2.x);
    v.TexCoords = vec2(p2.y, p3.x);
    v.Normal = vec3(p3.y, p4);
    v.Tangent = vec3(p5, p6.x);
    v.Bitangent = vec3(p6.y, p7);
    v.BoneIndices = uint[4](floatBitsToInt(p8.x), floatBitsToInt(p8.y), floatBitsToInt(p9.x), floatBitsToInt(p9.y));
    v.BoneWeights = float[4](p10.x, p10.y, p11.x, p11.y);

    return v;
}

void writeVertex(VertexWriteBuffer vertices, uint index, Vertex vertex)
{
    const vec2 p1 = vertex.Position.xy;
    const vec2 p2 = vec2(vertex.Position.z, vertex.TexCoords.x);
    const vec2 p3 = vec2(vertex.TexCoords.y, vertex.Normal.x);
    const vec2 p4 = vertex.Normal.yz;
    const vec2 p5 = vertex.Tangent.xy;
    const vec2 p6 = vec2(vertex.Tangent.z, vertex.Bitangent.x);
    const vec2 p7 = vec2(vertex.Bitangent.yz);

    vertices.v[index * 7] = p1;
    vertices.v[index * 7 + 1] = p2;
    vertices.v[index * 7 + 2] = p3;
    vertices.v[index * 7 + 3] = p4;
    vertices.v[index * 7 + 4] = p5;
    vertices.v[index * 7 + 5] = p6;
    vertices.v[index * 7 + 6] = p7;
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

uint GetColorTextureIndex(uint HitGroupFlags, TexturedMaterial material)
{
    return (HitGroupFlags & HitGroupFlagsDisableColorTexture) == 0 ? material.ColorIdx : DefaultColorTextureIndex;
}

uint GetNormalTextureIndex(uint HitGroupFlags, TexturedMaterial material)
{
    return (HitGroupFlags & HitGroupFlagsDisableNormalTexture) == 0 ? material.NormalIdx : DefaultNormalTextureIndex;
}

uint GetRoughnessTextureIndex(uint HitGroupFlags, TexturedMaterial material)
{
    return (HitGroupFlags & HitGroupFlagsDisableRoughnessTexture) == 0 ? material.RoughnessIdx : DefaultRoughnessTextureIndex;
}

uint GetMetalicTextureIndex(uint HitGroupFlags, TexturedMaterial material)
{
    return (HitGroupFlags & HitGroupFlagsDisableMetalicTexture) == 0 ? material.MetalicIdx : DefaultMetalicTextureIndex;
}

Vertex getInterpolatedVertex(VertexBuffer vertices, IndexBuffer indices, uint indexOffset, vec3 barycentricCoords)
{
    return interpolate(
        getVertex(vertices, indices, indexOffset), getVertex(vertices, indices, indexOffset + 1),
        getVertex(vertices, indices, indexOffset + 2), barycentricCoords
    );
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
