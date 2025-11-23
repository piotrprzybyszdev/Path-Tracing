#include "common.glsl"

const float DirectionalLightDistance = 100000.0f;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0001);
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

vec3 specularBrdf(vec3 lightDir, vec3 V, vec3 N, vec3 color, float roughness, float metalness)
{
	const vec3 L = -normalize(lightDir);

    const vec3 H = normalize(V + L);

    const vec3 R = 2.0f * dot(L, N) * N - L;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, color, metalness);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    vec3 specular = numerator / max(denominator, 0.0001);

    return specular;
}

vec3 computeLightContribution(vec3 lightDir, vec3 lightColor, float attenuation, vec3 position, vec3 V, vec3 N, vec3 color, float roughness, float metalness)
{
    const vec3 L = -normalize(lightDir);

    const vec3 H = normalize(V + L);

    const vec3 R = 2.0f * dot(L, N) * N - L;

    const vec3 radiance = lightColor * attenuation;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, color, metalness);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0);
    vec3 specular = numerator / max(denominator, 0.0001);

    vec3 kS = F;

    vec3 kD = vec3(1.0) - kS;

    kD *= 1.0 - metalness;

    float NdotL = max(dot(N, L), 0.0);

    return (kD * color / PI + specular) * radiance * NdotL;
}

float getBrdfProbability(vec3 color, float roughness, float metalness, vec3 V, vec3 N) {
	
	// Evaluate Fresnel term using the shading normal
	// Note: we use the shading normal instead of the microfacet normal (half-vector) for Fresnel term here. That's suboptimal for rough surfaces at grazing angles, but half-vector is yet unknown at this point
    vec3 F0 = vec3(0.04);
	F0 = mix(F0, color, metalness);
    float diffuseReflectance = luminance(color * (1.0f - metalness));
    float Fresnel = luminance(fresnelSchlick(dot(V, N), F0));

	// Approximate relative contribution of BRDFs using the Fresnel term
	float specular = Fresnel;
	float diffuse = diffuseReflectance * (1.0f - Fresnel); //< If diffuse term is weighted by Fresnel, apply it here as well

	// Return probability of selecting specular BRDF over diffuse BRDF
	float p = (specular / max(0.0001f, (specular + diffuse)));

	// Clamp probability to avoid undersampling of less prominent BRDF
	return clamp(p, 0.1f, 0.9f);
}

// Samples a direction within a hemisphere oriented along +Z axis with a cosine-weighted distribution 
// Source: "Sampling Transformations Zoo" in Ray Tracing Gems by Shirley et al.
vec3 sampleHemisphere(vec2 u) {

	float a = sqrt(u.x);
	float b = 2.0f * PI * u.y;

	vec3 result = vec3(
		a * cos(b),
		a * sin(b),
		sqrt(1.0f - u.x));

	// pdf = result.z / PI;

	return result;
}

// Samples a microfacet normal for the GGX distribution using VNDF method.
// Source: "Sampling the GGX Distribution of Visible Normals" by Heitz
// See also https://hal.inria.fr/hal-00996995v1/document and http://jcgt.org/published/0007/04/01/
// Random variables 'u' must be in <0;1) interval
// PDF is 'G1(NdotV) * D'
vec3 sampleGGXVNDF(vec3 Ve, vec2 alpha2D, vec2 u) {

	// Section 3.2: transforming the view direction to the hemisphere configuration
	vec3 Vh = normalize(vec3(alpha2D.x * Ve.x, alpha2D.y * Ve.y, Ve.z));

	// Section 4.1: orthonormal basis (with special case if cross product is zero)
	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	vec3 T1 = lensq > 0.0f ? vec3(-Vh.y, Vh.x, 0.0f) * inversesqrt(lensq) : vec3(1.0f, 0.0f, 0.0f);
	vec3 T2 = cross(Vh, T1);

	// Section 4.2: parameterization of the projected area
	float r = sqrt(u.x);
	float phi = 2 * PI * u.y;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5f * (1.0f + Vh.z);
	t2 = mix(sqrt(1.0f - t1 * t1), t2, s);

	// Section 4.3: reprojection onto hemisphere
	vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

	// Section 3.4: transforming the normal back to the ellipsoid configuration
	return normalize(vec3(alpha2D.x * Nh.x, alpha2D.y * Nh.y, max(0.0f, Nh.z)));
}

float Smith_G1_GGX(float alpha, float NdotS) {
	float alphaSquared = alpha * alpha;
	float NdotSSquared = NdotS * NdotS;
	return 2.0f / (sqrt(((alphaSquared * (1.0f - NdotSSquared)) + NdotSSquared) / NdotSSquared) + 1.0f);
}

vec3 sampleSpecularMicrofacet(vec3 Vlocal, float roughness, vec3 F0, vec2 u, out vec3 weight) {

	// Sample a microfacet normal (H) in local space
	vec3 Hlocal;
	float alpha = roughness * roughness;
	if (alpha == 0.0f) {
		// Fast path for zero roughness (perfect reflection), also prevents NaNs appearing due to divisions by zeroes
		Hlocal = vec3(0.0f, 0.0f, 1.0f);
	} else {
		// For non-zero roughness, this calls VNDF sampling for GG-X distribution or Walter's sampling for Beckmann distribution
		Hlocal = sampleGGXVNDF(Vlocal, vec2(alpha, alpha), u);
	}

	// Reflect view direction to obtain light vector
	vec3 Llocal = reflect(-Vlocal, Hlocal);

	// Note: HdotL is same as HdotV here
	// Clamp dot products here to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
	float HdotL = max(0.00001f, min(1.0f, dot(Hlocal, Llocal)));
	const vec3 Nlocal = vec3(0.0f, 0.0f, 1.0f);
	float NdotL = max(0.00001f, min(1.0f, dot(Nlocal, Llocal)));
	float NdotV = max(0.00001f, min(1.0f, dot(Nlocal, Vlocal)));
	float NdotH = max(0.00001f, min(1.0f, dot(Nlocal, Hlocal)));
	vec3 F = fresnelSchlick(HdotL, F0);

	// Calculate weight of the sample specific for selected sampling method 
	// (this is microfacet BRDF divided by PDF of sampling method - notice how most terms cancel out)
	weight = F * Smith_G1_GGX(alpha, NdotL);

	return Llocal;
}

bool evalIndirectCombinedBRDF(vec2 u, vec3 shadingNormal, vec3 geometryNormal, vec3 V, vec3 color, float roughness, float metalness, const uint brdfType, out vec3 rayDirection, out vec3 sampleWeight) {

	// Ignore incident ray coming from "below" the hemisphere
	// if (dot(geometryNormal, V) <= 0.0f) return false;

	// Transform view direction into local space of our sampling routines 
	// (local space is oriented so that its positive Z axis points along the shading normal)
	vec4 qRotationToZ = getRotationToZAxis(shadingNormal);
	vec3 Vlocal = rotatePoint(qRotationToZ, V);
	const vec3 Nlocal = vec3(0.0f, 0.0f, 1.0f);

	vec3 rayDirectionLocal = vec3(0.0f, 0.0f, 0.0f);

	if (brdfType == BrdfTypeDiffuse) {

		// Sample diffuse ray using cosine-weighted hemisphere sampling 
		rayDirectionLocal = sampleHemisphere(u);

		// Function 'diffuseTerm' is predivided by PDF of sampling the cosine weighted hemisphere
		sampleWeight = color * (1.0f - metalness);

		// Sample a half-vector of specular BRDF. Note that we're reusing random variable 'u' here, but correctly it should be an new independent random number
		float alpha = roughness * roughness;
		vec3 Hspecular = sampleGGXVNDF(Vlocal, vec2(alpha, alpha), u);

		// Clamp HdotL to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
		float VdotH = max(0.00001f, min(1.0f, dot(Vlocal, Hspecular)));
	    vec3 F0 = vec3(0.04);
		F0 = mix(F0, color, metalness);
		sampleWeight *= (vec3(1.0f, 1.0f, 1.0f) - fresnelSchlick(VdotH, F0));

	} else if (brdfType == BrdfTypeSpecular) {
		vec3 F0 = vec3(0.04);
		F0 = mix(F0, color, metalness);
		rayDirectionLocal = sampleSpecularMicrofacet(Vlocal, roughness, F0, u, sampleWeight);
	}

	// Prevent tracing direction with no contribution
	if (luminance(sampleWeight) == 0.0f) return false;

	// Transform sampled direction Llocal back to V vector space
	rayDirection = normalize(rotatePoint(invertRotation(qRotationToZ), rayDirectionLocal));

	// Prevent tracing direction "under" the hemisphere (behind the triangle)
	// if (dot(geometryNormal, rayDirection) <= 0.0f) return false;

	return true;
}

vec3 getRandomUnitSphere(inout uint rngState)
{
    // Uniform sampling on a unit sphere:
    // z in [-1,1], phi in [0, 2PI)
    float z = 2.0f * rand(rngState) - 1.0f;
    float phi = 2.0f * PI * rand(rngState);
    float r = sqrt(max(0.0f, 1.0f - z * z));
    return vec3(r * cos(phi), r * sin(phi), z);
}