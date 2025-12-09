#include "ShaderRendererTypes.incl"

const float PI = 3.14159265359f;

const float MISS_HIT_DISTANCE = -1.0f;

float luminance(vec3 rgb)
{
	return dot(rgb, vec3(0.2126f, 0.7152f, 0.0722f));
}

float maxComponent(vec3 rgb)
{
    return max(rgb.r, max(rgb.g, rgb.b));
}

vec3 hdrToLdr(vec3 rgb)
{
    return rgb / (1.0f + maxComponent(rgb));
}

vec3 computeBarycentricCoords(vec3 attribs)
{
    return vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
}

Vertex getVertex(VertexBuffer vertices, IndexBuffer indices, uint offset)
{
    const uint index = indices.v[offset];
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

vec2 getTexCoords(VertexBuffer vertices, IndexBuffer indices, uint offset)
{
    const uint index = indices.v[offset];
    const float p2 = vertices.v[index * 7 + 1].y;
    const float p3 = vertices.v[index * 7 + 2].x;

    return vec2(p2, p3);
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

Vertex getInterpolatedVertex(VertexBuffer vertices, IndexBuffer indices, uint indexOffset, vec3 barycentricCoords)
{
    return interpolate(
        getVertex(vertices, indices, indexOffset), getVertex(vertices, indices, indexOffset + 1),
        getVertex(vertices, indices, indexOffset + 2), barycentricCoords
    );
}

uint jenkinsHash(uint x)
{
    x += x << 10;
    x ^= x >> 6;
    x += x << 3;
    x ^= x >> 11;
    x += x << 15;
    return x;
}

uint initRng(uvec2 pixel, uvec2 resolution, uint frame)
{
    uint rngState = uint(dot(pixel, uvec2(1, resolution.x))) ^ jenkinsHash(frame);
    return jenkinsHash(rngState);
}

float uintToFloat(uint x)
{
    return uintBitsToFloat(0x3f800000 | (x >> 9)) - 1.0f;
}

uint xorshift(inout uint rngState)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

float rand(inout uint rngState)
{
    return uintToFloat(xorshift(rngState));
}

vec2 sampleUniformDiskConcentric(vec2 u)
{
    vec2 offset = 2.0f * u - 1.0f;
    if (offset == vec2(0.0f))
        return vec2(0.0f);

    if (abs(offset.x) > abs(offset.y))
    {
        float theta = PI / 4 * (offset.y / offset.x);
        return offset.x * vec2(cos(theta), sin(theta));
    }
    else
    {
        float theta = PI / 2 - PI / 4 * (offset.x / offset.y);
        return offset.y * vec2(cos(theta), sin(theta));
    }
}

vec3 sampleCosineHemisphere(vec2 u)
{
    vec2 d = sampleUniformDiskConcentric(u);
    float z = sqrt(1 - d.x * d.x - d.y * d.y);
    return vec3(d, z);
}

mat3 computeTangentSpace(vec3 normal)
{
    vec3 t1 = cross(normal, vec3(1.0f, 0.0f, 0.0f));
    vec3 t2 = cross(normal, vec3(0.0f, 1.0f, 0.0f));

    vec3 tangent = length(t1) > length(t2) ? t1 : t2;
    vec3 bitangent = cross(normal, tangent);
    
    return mat3(normalize(tangent), normalize(bitangent), normal);
}
