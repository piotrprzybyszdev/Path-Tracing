#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require

#include "ShaderRendererTypes.incl"
#include "DebugShaderTypes.incl"

layout(constant_id = DebugMissFlagsConstantId) const uint s_MissFlags = MissFlagsNone;

layout(binding = 10, set = 0) uniform sampler2D skybox2D;

layout(binding = 11, set = 0) uniform samplerCube skyboxCube;

layout(location = 0) rayPayloadInEXT DebugPayload payload;

#include "common.glsl"

void main()
{
    if ((s_MissFlags & MissFlagsSkybox2D) != MissFlagsNone)
    {
        const vec3 dir = gl_WorldRayDirectionEXT;

        const float longitude = atan(dir.z, dir.x);
        const float latitude  = asin(-dir.y);

        const vec2 texCoords = vec2(longitude / 2.0f, latitude) / PI + 0.5f;

        payload.Color = vec4(texture(skybox2D, texCoords).xyz, 1.0f);
    }
    else if ((s_MissFlags & MissFlagsSkyboxCube) != MissFlagsNone)
    {
        payload.Color = vec4(texture(skyboxCube, gl_WorldRayDirectionEXT).xyz, 1.0f);
    }
    else
        payload.Color = vec4(0.2f, 0.2f, 0.2f, 1.0f);
}
