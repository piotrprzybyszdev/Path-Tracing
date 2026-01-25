#include "common.glsl"

const float origin_const = 1.0f / 32.0f;
const float float_scale = 1.0f / 65536.0f;
const float int_scale = 256.0f;

struct Ray
{
    vec3 Origin;
    float tmin;
    vec3 Direction;
    float tmax;
};

// https://www.pbr-book.org/4ed/Cameras_and_Film/Projective_Camera_Models#TheThinLensModelandDepthofField
Ray constructPrimaryRay(uvec2 pixel, uvec2 resolution, Camera camera, vec2 u, vec2 u2, float lensRadius, float focalDistance, out Ray rx, out Ray ry)
{
    const vec2 pixelCenter = pixel + u;
    const vec2 pixelCenterOffsetX = pixelCenter + vec2(1.0, 0.0);
    const vec2 pixelCenterOffsetY = pixelCenter + vec2(0.0, 1.0);

    const vec2 pLens = lensRadius * sampleUniformDiskConcentric(u2);

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

// https://doi.org/10.1007/978-1-4842-4427-2_6
vec3 offsetRayOriginSelfIntersection(vec3 origin, vec3 normal)
{
    ivec3 of_i = ivec3(int_scale * normal);
    vec3 p_i = vec3(
        intBitsToFloat(floatBitsToInt(origin.x) + ((origin.x < 0) ? -of_i.x : of_i.x)),
        intBitsToFloat(floatBitsToInt(origin.y) + ((origin.y < 0) ? -of_i.y : of_i.y)),
        intBitsToFloat(floatBitsToInt(origin.z) + ((origin.z < 0) ? -of_i.z : of_i.z))
    );
    return vec3(
        (abs(origin.x) < origin_const) ? origin.x + float_scale * normal.x : p_i.x,
        (abs(origin.y) < origin_const) ? origin.y + float_scale * normal.y : p_i.y,
        (abs(origin.z) < origin_const) ? origin.z + float_scale * normal.z : p_i.z
    );
}

// https://doi.org/10.1007/978-1-4842-7185-8_4
vec3 offsetRayOriginShadowTerminator(Vertex vertex, Vertex v0, Vertex v1, Vertex v2, vec3 barycentricCoords, bool isRefracted)
{   
    vec3 tmpu = vertex.Position - v0.Position;
    vec3 tmpv = vertex.Position - v1.Position;
    vec3 tmpw = vertex.Position - v2.Position;

    if (isRefracted)
    {
        v0.Normal *= -1;
        v1.Normal *= -1;
        v2.Normal *= -1;
    }

    float dotu = min(0.0f, dot(tmpu, v0.Normal));
    float dotv = min(0.0f, dot(tmpv, v1.Normal));
    float dotw = min(0.0f, dot(tmpw, v2.Normal));

    tmpu -= dotu * v0.Normal;
    tmpv -= dotv * v1.Normal;
    tmpw -= dotw * v2.Normal;

    return vertex.Position + barycentricCoords.x * tmpu + barycentricCoords.y * tmpv + barycentricCoords.z * tmpw;
}
