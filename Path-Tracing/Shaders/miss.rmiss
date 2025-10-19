#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require

#include "ShaderRendererTypes.incl"
#include "common.glsl"

layout(constant_id = MissFlagsConstantId) const uint s_MissFlags = MissFlagsNone;

layout(binding = 9, set = 0) uniform sampler2D skybox2D;

layout(binding = 10, set = 0) uniform samplerCube skyboxCube;

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
    if ((s_MissFlags & MissFlagsSkybox2D) != MissFlagsNone)
    {
        const vec3 dir = gl_WorldRayDirectionEXT;

        const float longitude = atan(dir.z, dir.x);
        const float latitude  = asin(-dir.y);

        const vec2 texCoords = vec2(longitude / 2.0f, latitude) / PI + 0.5f;

        hitValue = texture(skybox2D, texCoords).xyz;
    }
    else if ((s_MissFlags & MissFlagsSkyboxCube) != MissFlagsNone)
    {
        hitValue = texture(skyboxCube, gl_WorldRayDirectionEXT).xyz;
    }
    else
        hitValue = vec3(0.2f, 0.2f, 0.2f);
}
