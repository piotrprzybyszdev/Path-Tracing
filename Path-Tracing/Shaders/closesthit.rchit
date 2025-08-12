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

layout(shaderRecordEXT, std430) buffer SBT {
	SBTBuffer sbt;
};

layout(location = 0) rayPayloadInEXT vec3 hitValue;
hitAttributeEXT vec3 attribs;

Vertex transform(Vertex vertex, uint transformIndex)
{
    const mat3x4 transform = mat3x4(transforms[transformIndex] * gl_ObjectToWorldEXT);

    vertex.Position = (transform * vertex.Position).xyz;
    vertex.Tangent = normalize((transform * vertex.Tangent).xyz);
    vertex.Bitangent = normalize((transform * vertex.Bitangent).xyz);
    vertex.Normal = normalize((transpose(inverse(mat4(transform))) * vec4(vertex.Normal, 1.0f)).xyz);  // TODO: Calculate inverse on CPU

    return vertex;
}

void main()
{
	const vec3 barycentricCoords = computeBarycentricCoords(attribs);

	VertexBuffer vertices = VertexBuffer(geometries[sbt.GeometryIndex].Vertices);
	IndexBuffer indices = IndexBuffer(geometries[sbt.GeometryIndex].Indices);

	const Vertex originalVertex = getInterpolatedVertex(vertices, indices, gl_PrimitiveID * 3, barycentricCoords);
	const Vertex vertex = transform(originalVertex, sbt.TransformIndex);

	// TODO: Calculate the LOD level properly
	const float lod = (mainUniform.u_Flags & ClosestHitFlagsDisableMipMaps) != ClosestHitFlagsNone ? 1.0f : log2(gl_RayTmaxEXT);

	const Material material = materials[sbt.MaterialIndex];
	const vec3 color = textureLod(textures[GetColorTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords, lod).xyz;
	const vec3 normal = textureLod(textures[GetNormalTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords, lod).xyz;
	const vec3 roughness = texture(textures[GetRoughnessTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords).xyz;
	const vec3 metalness = texture(textures[GetMetalicTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords).xyz;

	const vec3 lightColor = vec3(1.0f);
	const vec3 viewDir = gl_WorldRayDirectionEXT;
	const vec3 lightPos = vec3(3.0f, 15.0f, 7.0f);
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
		hitValue = ambient * color + (diffuse + specular) * lightColor * color;
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
