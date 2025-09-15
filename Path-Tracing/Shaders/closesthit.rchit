#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderRendererTypes.incl"
#include "common.glsl"

layout(binding = 3, set = 0) uniform MainBlock {
	ClosestHitUniformData mainUniform;
};

layout(binding = 4, set = 0) uniform sampler2D textures[];

layout(binding = 5, set = 0) readonly buffer TransformBuffer {
	mat3x4[] transforms;
};

layout(binding = 6, set = 0) readonly buffer GeometryBuffer {
	Geometry[] geometries;
};

layout(binding = 7, set = 0) readonly buffer MaterialBuffer {
	Material[] materials;
};

layout(binding = 8, set = 0) uniform LightsBuffer {
	Light[MaxLightCount] lights;
};

layout(shaderRecordEXT, std430) buffer SBT {
	SBTBuffer sbt;
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec3 attribs;

Vertex transform(Vertex vertex, uint transformIndex)
{
    const mat3x4 transform = mat3x4(transforms[transformIndex] * gl_ObjectToWorldEXT);

	vertex.Position = vec4(vertex.Position, 1.0f) * transform;
    vertex.Tangent = normalize(vec4(vertex.Tangent, 0.0f) * transform);
    vertex.Bitangent = normalize(vec4(vertex.Bitangent, 0.0f) * transform);
    vertex.Normal = normalize((vec4(vertex.Normal, 0.0f) * transpose(inverse(mat4(transform)))).xyz);  // TODO: Calculate inverse on the CPU

    return vertex;
}

vec3 computeLightContribution(Light light, vec3 position, vec3 V, vec3 N, mat3 TBN, vec3 color, vec3 roughness, vec3 metalness)
{
	const vec3 lightDir = position - light.Position;

	const vec3 L = -normalize(lightDir);

	const vec3 R = 2.0f * dot(L, N) * N - L;

	const float diffuse = 1.0f * max(dot(L, N), 0.0f);
	const float specular = 1.0f * max(pow(dot(R, V), 50.0f), 0.0f);

	const float dist = length(lightDir);
	const float attenuation = 1.0f / (light.AttenuationConstant + dist * light.AttenuationLinear + dist * dist * light.AttenuationQuadratic);

	return (diffuse + specular) * light.Color * color * attenuation;
}

void main()
{
	const vec3 barycentricCoords = computeBarycentricCoords(attribs);

	VertexBuffer vertices = VertexBuffer(geometries[sbt.GeometryIndex].Vertices);
	IndexBuffer indices = IndexBuffer(geometries[sbt.GeometryIndex].Indices);

	const Vertex originalVertex = getInterpolatedVertex(vertices, indices, gl_PrimitiveID * 3, barycentricCoords);
	const Vertex vertex = transform(originalVertex, sbt.TransformIndex);

	// TODO: Calculate the LOD properly
	const float lod = (mainUniform.u_Flags & ClosestHitFlagsDisableMipMaps) != ClosestHitFlagsNone ? 0.0f : log2(gl_RayTmaxEXT);

	const Material material = materials[sbt.MaterialIndex];
	const vec3 color = textureLod(textures[GetColorTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords, lod).xyz;
	const vec3 normal = textureLod(textures[GetNormalTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords, lod).xyz;
	const vec3 roughness = texture(textures[GetRoughnessTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords).xyz;
	const vec3 metalness = texture(textures[GetMetalicTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords).xyz;

	const vec3 viewDir = gl_WorldRayDirectionEXT;
	const vec3 V = -normalize(viewDir);
	const mat3 TBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
	const vec3 N = normalize(vertex.Normal + TBN * (2.0f * normal - 1.0f));

	const float ambient = 0.1f;
	vec3 totalLight = ambient * color;
	for (uint lightIndex = 0; lightIndex < mainUniform.u_LightCount; lightIndex++)
	{
		const vec3 lightContribution = computeLightContribution(lights[lightIndex], vertex.Position, V, N, TBN, color, roughness, metalness);
		totalLight += lightContribution;
	}

	switch (mainUniform.u_RenderMode)
	{
	case RenderModeColor:
		hitValue = totalLight;
		break;
	case RenderModeWorldPosition:
		hitValue = vertex.Position.xyz;
		break;
	case RenderModeNormal:
		hitValue = N;
		break;
	case RenderModeTextureCoords:
		hitValue = vec3(vertex.TexCoords, 0.0f);
		break;
	case RenderModeMips:
		hitValue = vec3(floor(lod) / textureQueryLevels(textures[GetColorTextureIndex(mainUniform.u_EnabledTextures, material)]));
		break;
	}
}
