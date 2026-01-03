#include "common.glsl"

struct Ray
{
    vec3 Origin;
    float tmin;
    vec3 Direction;
    float tmax;
};

Ray constructPrimaryRay(uvec2 pixel, uvec2 resolution, Camera camera, vec2 u, vec2 u2, float lensRadius, float focalDistance, out Ray rx, out Ray ry)
{
    const vec2 pixelCenter = pixel + u;
    const vec2 pixelCenterOffsetX = pixelCenter + vec2(1.0, 0.0);
    const vec2 pixelCenterOffsetY = pixelCenter + vec2(0.0, 1.0);

    const vec2 pLens = lensRadius * SampleUniformDiskConcentric(u2);

    const vec2 inUV = pixelCenter / resolution;
    vec2 d = inUV * 2.0 - 1.0;
    const vec2 inUVOffsetX = pixelCenterOffsetX / resolution;
    vec2 dOffsetX = inUVOffsetX * 2.0 - 1.0;
    const vec2 inUVOffsetY = pixelCenterOffsetY / resolution;
    vec2 dOffsetY = inUVOffsetY * 2.0 - 1.0;

    vec3 originCameraSpace = vec3(pLens.x, pLens.y, 0);
    vec3 origin = (camera.ViewInverse * vec4(originCameraSpace, 1)).xyz;
    vec3 originLensCenter = (camera.ViewInverse * vec4(0, 0, 0, 1)).xyz;
    
    vec3 target = (camera.ProjInverse * vec4(d.x, d.y, 1, 1)).xyz;
    float ft = focalDistance / target.z;
    vec3 pFocus = ft * target;
    vec3 direction = (camera.ViewInverse * vec4(normalize(pFocus - originCameraSpace), 0)).xyz;

    vec3 targetOffsetX = (camera.ProjInverse * vec4(dOffsetX.x, dOffsetX.y, 1, 1)).xyz;
    float ftOffsetX = focalDistance / targetOffsetX.z;
    vec3 pFocusOffsetX = ftOffsetX * targetOffsetX;
    vec3 directionOffsetX = (camera.ViewInverse * vec4(normalize(pFocusOffsetX - originCameraSpace), 0)).xyz;

    vec3 targetOffsetY = (camera.ProjInverse * vec4(dOffsetY.x, dOffsetY.y, 1, 1)).xyz;
    float ftOffsetY = focalDistance / targetOffsetY.z;
    vec3 pFocusOffsetY = ftOffsetY * targetOffsetY;
    vec3 directionOffsetY = (camera.ViewInverse * vec4(normalize(pFocusOffsetY - originCameraSpace), 0)).xyz;

    float tmin = 0.00001;
    float tmax = 10000.0;

    rx = Ray(origin, tmin, directionOffsetX, tmax);
    ry = Ray(origin, tmin, directionOffsetY, tmax);
    return Ray(origin, tmin, direction, tmax);
}

Ray constructPrimaryRay(uvec2 pixel, uvec2 resolution, Camera camera, vec2 u, out Ray rx, out Ray ry)
{
    const vec2 pixelCenter = pixel + u;
    const vec2 pixelCenterOffsetX = pixelCenter + vec2(1.0, 0.0);
    const vec2 pixelCenterOffsetY = pixelCenter + vec2(0.0, 1.0);

    const vec2 inUV = pixelCenter / resolution;
    vec2 d = inUV * 2.0 - 1.0;
    const vec2 inUVOffsetX = pixelCenterOffsetX / resolution;
    vec2 dOffsetX = inUVOffsetX * 2.0 - 1.0;
    const vec2 inUVOffsetY = pixelCenterOffsetY / resolution;
    vec2 dOffsetY = inUVOffsetY * 2.0 - 1.0;

    vec3 origin = (camera.ViewInverse * vec4(0, 0, 0, 1)).xyz;
    vec3 target = (camera.ProjInverse * vec4(d.x, d.y, 1, 1)).xyz;
    vec3 direction = (camera.ViewInverse * vec4(normalize(target), 0)).xyz;
    vec3 targetOffsetX = (camera.ProjInverse * vec4(dOffsetX.x, dOffsetX.y, 1, 1)).xyz;
    vec3 targetOffsetY = (camera.ProjInverse * vec4(dOffsetY.x, dOffsetY.y, 1, 1)).xyz;
    vec3 directionOffsetX = (camera.ViewInverse * vec4(normalize(targetOffsetX), 0)).xyz;
    vec3 directionOffsetY = (camera.ViewInverse * vec4(normalize(targetOffsetY), 0)).xyz;

    float tmin = 0.00001;
    float tmax = 10000.0;

    rx = Ray(origin, tmin, directionOffsetX, tmax);
    ry = Ray(origin, tmin, directionOffsetY, tmax);
    return Ray(origin, tmin, direction, tmax);
}

Ray constructPrimaryRay(uvec2 pixel, uvec2 resolution, Camera camera, out Ray rx, out Ray ry)
{
    return constructPrimaryRay(pixel, resolution, camera, vec2(0.5f), rx, ry);
}

// TODO: Fix the `shadow terminator problem` properly
vec3 offsetRayOrigin(vec3 origin, vec3 normal)
{
    return origin + normal * 0.01f;
}
