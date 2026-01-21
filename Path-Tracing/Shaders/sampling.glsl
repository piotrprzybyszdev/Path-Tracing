#include "Debug/DebugShaderTypes.incl"

const float DirectionalLightDistance = 100000.0f;

Vertex transform(Vertex vertex, uint transformIndex)
{
    const mat3x4 transform = mat3x4(mat4(transforms[transformIndex]) * gl_ObjectToWorld3x4EXT);

    vertex.Position = vec4(vertex.Position, 1.0f) * transform;
    vertex.Tangent = normalize(vec4(vertex.Tangent, 0.0f) * transform);
    vertex.Bitangent = normalize(vec4(vertex.Bitangent, 0.0f) * transform);
    vertex.Normal = normalize((vec4(vertex.Normal, 0.0f) * transpose(inverse(mat4(transform)))).xyz);  // TODO: Calculate inverse on the CPU

    return vertex;
}

struct LightSample
{
    vec3 Direction;
    float Distance;
    vec3 Color;
    float Attenuation;
};

LightSample sampleLight(float u, vec3 position, out float pdf)
{
    uint lightIndex = uint(u * (u_LightCount + 1));
    pdf = 1.0f / (u_LightCount + 1);
    LightSample ret;

    if (lightIndex >= u_LightCount)
    {
        ret.Direction = normalize(u_DirectionalLight.Direction);
        // TODO: Proper sampling
        ret.Color = u_DirectionalLight.Color;
        ret.Distance = DirectionalLightDistance;
        ret.Attenuation = 1.0f;
        return ret;
    }

    const PointLight light = u_Lights[lightIndex];

    ret.Distance = distance(position, light.Position);
    ret.Direction = normalize(position - light.Position);
    ret.Color = light.Color;
    const float attenuation = 1.0f / (light.AttenuationConstant + ret.Distance * light.AttenuationLinear + ret.Distance * ret.Distance * light.AttenuationQuadratic);
    ret.Attenuation = clamp(attenuation, 0.0f, 1.0f);
    
    return ret;
}
