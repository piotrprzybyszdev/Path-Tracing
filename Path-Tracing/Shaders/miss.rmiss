#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference : require

#include "ShaderRendererTypes.incl"

layout(location = 0) rayPayloadInEXT vec3 hitValue;

void main()
{
    hitValue = vec3(0.2, 0.2, 0.2);
}
