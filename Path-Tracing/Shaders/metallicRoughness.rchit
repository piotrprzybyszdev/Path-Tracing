#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ShaderRendererTypes.incl"

layout(constant_id = HitFlagsConstantId) const uint s_HitFlags = HitFlagsNone;

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
#include "material.glsl"
#include "tracing.glsl"
#include "bsdf.glsl"

void main()
{
    const vec3 barycentricCoords = computeBarycentricCoords(attribs);

    VertexBuffer vertices = VertexBuffer(geometries[sbt.GeometryIndex].Vertices);
    IndexBuffer indices = IndexBuffer(geometries[sbt.GeometryIndex].Indices);

    const Vertex originalVertex = getInterpolatedVertex(vertices, indices, gl_PrimitiveID * 3, barycentricCoords);
    Vertex vertex = transform(originalVertex, sbt.TransformIndex);

    const bool isHitFromInside = dot(vertex.Normal, gl_WorldRayDirectionEXT) > 0.0f;
    if (isHitFromInside)
    {
        vertex.Normal *= -1;
        vertex.Tangent *= -1;
        vertex.Bitangent *= -1;
    }

    const vec3 origin = gl_WorldRayOriginEXT;
    const vec3 viewDir = gl_WorldRayDirectionEXT;

    // Calculate geometric dP/du and dP/dv
    Vertex v0 = getVertex(vertices, indices, gl_PrimitiveID * 3);
    Vertex v1 = getVertex(vertices, indices, gl_PrimitiveID * 3 + 1);
    Vertex v2 = getVertex(vertices, indices, gl_PrimitiveID * 3 + 2);

    v0 = transform(v0, sbt.TransformIndex);
    v1 = transform(v1, sbt.TransformIndex);
    v2 = transform(v2, sbt.TransformIndex);

    vec3 tmpu = vertex.Position - v0.Position;
    vec3 tmpv = vertex.Position - v1.Position;
    vec3 tmpw = vertex.Position - v2.Position;

    float dotu = min(0.0f, dot(tmpu, v0.Normal));
    float dotv = min(0.0f, dot(tmpv, v1.Normal));
    float dotw = min(0.0f, dot(tmpw, v2.Normal));

    tmpu -= dotu * v0.Normal;
    tmpv -= dotv * v1.Normal;
    tmpw -= dotw * v2.Normal;

    vec3 Pp = vertex.Position + barycentricCoords.x * tmpu + barycentricCoords.y * tmpv + barycentricCoords.z * tmpw;

    vec3 dpdu, dpdv, dndu, dndv;
    computeDpnDuv(v0, v1, v2, vertex, dpdu, dpdv, dndu, dndv);

    vec3 rxOrigin = payload.RayDifferentials0.xyz;
    vec3 rxDirection = vec3(payload.RayDifferentials0.w, payload.RayDifferentials1.xy);
    vec3 ryOrigin = vec3(payload.RayDifferentials1.zw, payload.RayDifferentials2.x);
    vec3 ryDirection = payload.RayDifferentials2.yzw;

    vec3 dpdx, dpdy;
    computeDpDxy(vertex.Position, origin, normalize(viewDir), rxOrigin, rxDirection, ryOrigin, ryDirection, vertex.Normal, dpdx, dpdy);

    const vec4 derivatives = computeDerivatives(dpdx, dpdy, dpdu, dpdv);

    const bool flipYNormal = (s_HitFlags & HitFlagsDxNormalTextures) != HitFlagsNone;
    MaterialSample material = sampleMaterial(sbt.MaterialId, vertex.TexCoords, derivatives, 0, isHitFromInside, flipYNormal);
    
    // Handling decals
    if (payload.DirectLightPdf != -1.0f && gl_RayTmaxEXT > payload.DirectLightPdf)
        material.Color = mix(material.Color, payload.LightDirection.rgb, payload.LightDistance);
    
    // Prevents firefly artifacts
    payload.MaxRoughness = max(material.Roughness, payload.MaxRoughness);

    // Glossy lobe is numerically unstable at very low roughness
    material.Roughness = max(payload.MaxRoughness, 0.01f);  // TODO: Consider adding a perfect specular lobe
    
    const mat3 geometryTBN = mat3(vertex.Tangent, vertex.Bitangent, vertex.Normal);
    const vec3 N = normalize(vertex.Normal + geometryTBN * material.Normal);
    const mat3 TBN = computeTangentSpace(N);
    const vec3 V = normalize(inverse(TBN) * normalize(-gl_WorldRayDirectionEXT));
    
    uint rngState = payload.RngState;

    float lightPdf, lightSmplPdf;  // unused
    LightSample light = sampleLight(rand(rngState), Pp, lightPdf);
    const vec3 L = normalize(inverse(TBN) * -light.Direction);
    const vec3 lightBsdf = evaluateBSDF(material, V, L, lightSmplPdf);

    BSDFSample bsdf = sampleBSDF(material, V, rngState);

    payload.Direction = normalize(TBN * bsdf.Direction);
    payload.Position = Pp;
    payload.Bsdf = bsdf.Color;
    payload.Pdf = bsdf.Pdf;
    payload.Emissive = material.EmissiveColor;
    payload.RngState = rngState;
    payload.DirectLight = light.Color * light.Attenuation * lightBsdf;
    payload.DirectLightPdf = lightPdf;
    payload.LightDirection = light.Direction;
    payload.LightDistance = light.Distance;

    if (bsdf.Direction.z < 0.0f)
        computeRefractedDifferentialRays(derivatives, vertex.Normal, Pp, -viewDir, payload.Direction, dndu, dndv, material.Eta, rxOrigin, rxDirection, ryOrigin, ryDirection);
    else
        computeReflectedDifferentialRays(derivatives, vertex.Normal, Pp, -viewDir, payload.Direction, dndu, dndv, rxOrigin, rxDirection, ryOrigin, ryDirection);

    payload.RayDifferentials0 = vec4(rxOrigin, rxDirection.x);
    payload.RayDifferentials1 = vec4(rxDirection.yz, ryOrigin.xy);
    payload.RayDifferentials2 = vec4(ryOrigin.z, ryDirection);
}
