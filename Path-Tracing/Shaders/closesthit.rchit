#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderRendererTypes.incl"
#include "common.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT u_TopLevelAS;

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
layout(location = 1) rayPayloadEXT bool isOccluded;
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

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

bool checkOccluded(Light light, vec3 position)
{
	if ((mainUniform.u_Flags & ClosestHitFlagsDisableShadows) != ClosestHitFlagsNone)
		return false;

	vec3 direction = normalize(light.Position - position);

	float tmin = 0.001;
	float tmax = length(direction);

	isOccluded = true;

	traceRayEXT(u_TopLevelAS, gl_RayFlagsNoneEXT, 0xff, 1, 2, 1, position, tmin, direction, tmax, 1);

	return isOccluded;
}

vec3 computeLightContribution(Light light, vec3 position, vec3 V, vec3 N, mat3 TBN, vec3 color, float roughness, float metalness)
{
	if (checkOccluded(light, position))
		return vec3(0.0f);

	const vec3 lightDir = position - light.Position;

	const vec3 L = -normalize(lightDir);

	const vec3 H = normalize(V + L);

	const vec3 R = 2.0f * dot(L, N) * N - L;

	const float dist = length(lightDir);
	const float attenuation = 1.0f / (light.AttenuationConstant + dist * light.AttenuationLinear + dist * dist * light.AttenuationQuadratic);

	const vec3 radiance = light.Color * attenuation;

	vec3 F0 = vec3(0.04);	
    F0 = mix(F0, color, metalness);

    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
           
    vec3 numerator    = NDF * G * F; 
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // + 0.0001 to prevent divide by zero
    vec3 specular = numerator / denominator;

    vec3 kS = F;

    vec3 kD = vec3(1.0) - kS;

    kD *= 1.0 - metalness;

    float NdotL = max(dot(N, L), 0.0);        

    return (kD * color / PI + specular) * radiance * NdotL;
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
	const float roughness = textureLod(textures[GetRoughnessTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords, lod).y;
	const float metalness = textureLod(textures[GetMetalicTextureIndex(mainUniform.u_EnabledTextures, material)], vertex.TexCoords, lod).z;

	const vec3 viewDir = gl_WorldRayDirectionEXT;
	const vec3 V = -normalize(viewDir);
	const mat3 TBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
	const vec3 N = normalize(vertex.Normal + TBN * (2.0f * normal - 1.0f));

	const float ambient = 0.05f;
	vec3 totalLight = color * ambient;
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
	case RenderModeGeometry:
		hitValue = getRandomColor(gl_GeometryIndexEXT);
		break;
	case RenderModePrimitive:
		hitValue = getRandomColor(gl_PrimitiveID);
		break;
	case RenderModeInstance:
		hitValue = getRandomColor(gl_InstanceID);
		break;
	}
}
