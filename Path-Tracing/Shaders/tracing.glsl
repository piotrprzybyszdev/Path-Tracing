#include "ShaderRendererTypes.incl"

// TODO: Fix the `shadow terminator problem` properly
vec3 offsetRayOrigin(vec3 origin, vec3 normal)
{
    return origin + normal * 0.01f;
}

bool checkOccluded(vec3 lightDir, vec3 position, float dist)
{
    if ((s_HitGroupFlags & HitGroupFlagsDisableShadows) != HitGroupFlagsNone)
        return false;
    
    vec3 direction = -normalize(lightDir);

    float tmin = 0.00001;
    float tmax = dist;

    isOccluded = true;

    traceRayEXT(u_TopLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xff, OcclusionRayHitGroupIndex, 2, OcclusionRayMissGroupIndex, position, tmin, direction, tmax, 1);

    return isOccluded;
}
