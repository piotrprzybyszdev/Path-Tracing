bool checkOccluded(vec3 lightDir, vec3 position, float dist)
{  
    vec3 direction = -normalize(lightDir);

    float tmin = 0.00001;
    float tmax = dist;

    isOccluded = true;

    traceRayEXT(u_TopLevelAS, gl_RayFlagsTerminateOnFirstHitEXT, 0xff, OcclusionRayHitGroupIndex, 2, OcclusionRayMissGroupIndex, position, tmin, direction, tmax, 1);

    return isOccluded;
}
