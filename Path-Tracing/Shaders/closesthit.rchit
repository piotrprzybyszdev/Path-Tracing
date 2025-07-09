#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderTypes.incl"

layout(binding = 3, set = 0) uniform MainBlock {
	ClosestHitUniformData mainUniform;
};

layout(binding = 4, set = 0) readonly buffer VertexBuffer {
	vec2[] vertices;
};

layout(binding = 5, set = 0) readonly buffer IndexBuffer {
	uint[] indices;
};

layout(binding = 6, set = 0) uniform sampler2D textures[];

layout(binding = 7, set = 0) readonly buffer GeometryBuffer {
	Geometry[] geometries;
};

layout(binding = 8, set = 0) readonly buffer MaterialBuffer {
	Material[] materials;
};

layout(shaderRecordEXT, std430) buffer SBT {
	SBTBuffer sbt;
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec3 attribs;

Vertex getVertex(uint index)
{
	const vec2 p1 = vertices[index * 7];
	const vec2 p2 = vertices[index * 7 + 1];
	const vec2 p3 = vertices[index * 7 + 2];
	const vec2 p4 = vertices[index * 7 + 3];
	const vec2 p5 = vertices[index * 7 + 4];
	const vec2 p6 = vertices[index * 7 + 5];
	const vec2 p7 = vertices[index * 7 + 6];

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
	v.Position = interpolate(v1.Position, v2.Position, v3.Position, barycentricCoords);
	v.TexCoords = interpolate(v1.TexCoords, v2.TexCoords, v3.TexCoords, barycentricCoords);
	v.Normal = interpolate(v1.Normal, v2.Normal, v3.Normal, barycentricCoords);
	v.Tangent = interpolate(v1.Tangent, v2.Tangent, v3.Tangent, barycentricCoords);
	v.Bitangent = interpolate(v1.Bitangent, v2.Bitangent, v3.Bitangent, barycentricCoords);

	return v;
}

float sampleTexture(uint index, vec2 texCoords, uint enabledFlag, float fallback)
{
	const bool isEnabled = (mainUniform.u_EnabledTextures & enabledFlag) != 0;
	return isEnabled ? texture(textures[index], texCoords).x : fallback;
}

vec2 sampleTexture(uint index, vec2 texCoords, uint enabledFlag, vec2 fallback)
{
	const bool isEnabled = (mainUniform.u_EnabledTextures & enabledFlag) != 0;
	return isEnabled ? texture(textures[index], texCoords).xy : fallback;
}

vec3 sampleTexture(uint index, vec2 texCoords, uint enabledFlag, vec3 fallback)
{
	const bool isEnabled = (mainUniform.u_EnabledTextures & enabledFlag) != 0;
	return isEnabled ? texture(textures[index], texCoords).xyz : fallback;
}

vec4 sampleTexture(uint index, vec2 texCoords, uint enabledFlag, vec4 fallback)
{
	const bool isEnabled = (mainUniform.u_EnabledTextures & enabledFlag) != 0;
	return isEnabled ? texture(textures[index], texCoords) : fallback;
}

void main()
{
	const Geometry geometry = geometries[sbt.GeometryIndex];
	const Material material = materials[sbt.MaterialIndex];
	
	const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

	const uint index1 = indices[geometry.IndexBufferOffset + gl_PrimitiveID * 3];
	const uint index2 = indices[geometry.IndexBufferOffset + gl_PrimitiveID * 3 + 1];
	const uint index3 = indices[geometry.IndexBufferOffset + gl_PrimitiveID * 3 + 2];

	const Vertex vertex = interpolate(
		getVertex(geometry.VertexBufferOffset + index1), getVertex(geometry.VertexBufferOffset + index2),
		getVertex(geometry.VertexBufferOffset + index3), barycentricCoords
	);
	
	const vec3 albedo = sampleTexture(material.AlbedoIdx, vertex.TexCoords, TexturesEnableAlbedo, DefaultAlbedo);
	const vec3 normal = sampleTexture(material.NormalIdx, vertex.TexCoords, TexturesEnableNormal, DefaultNormal);
	const float metalness = sampleTexture(material.MetalicIdx, vertex.TexCoords, TexturesEnableMetalic, DefaultMetalness);
	const float roughness = sampleTexture(material.RoughnessIdx, vertex.TexCoords, TexturesEnableRoughness, DefaultRoughness);

	const vec3 lightColor = vec3(1.0f);
	const vec3 viewDir = gl_WorldRayDirectionEXT;
	const vec3 lightPos = vec3(-100.0f, -100.0f, 100.0f);
	const vec3 lightDir = vertex.Position - lightPos;

	const mat3 TBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
	const vec3 N = normalize(vertex.Normal + TBN * (2.0f * normal - 1.0f));
	
	const vec3 L = -normalize(lightDir);
	const vec3 V = -normalize(viewDir);

	const vec3 R = 2.0f * dot(L, N) * N - L;

	const float ambient = 0.1f;
	const float diffuse = 1.0f * max(dot(L, N), 0.0f);
	const float specular = 1.0f * max(pow(dot(R, V), 50.0f), 0.0f);

	switch (mainUniform.u_RenderMode)
	{
	case RenderModeColor:
		hitValue = ambient * albedo + (diffuse + specular) * lightColor * albedo;
		break;
	case RenderModeWorldPosition:
		hitValue = (gl_ObjectToWorldEXT * vec4(vertex.Position, 1.0f)).xyz;
		break;
	case RenderModeNormal:
		hitValue = (N + 1.0f) / 2.0f;
		break;
	case RenderModeTextureCoords:
		hitValue = vec3(vertex.TexCoords, 0.0f);
		break;
	}
}
