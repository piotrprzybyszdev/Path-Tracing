struct Ray
{
    vec3 Origin;
    float tmin;
    vec3 Direction;
    float tmax;
};

Ray constructPrimaryRay(uvec2 pixel, uvec2 resolution, Camera camera, vec2 u)
{
    const vec2 pixelCenter = pixel + u;
    const vec2 inUV = pixelCenter / resolution;
    vec2 d = inUV * 2.0 - 1.0;

    vec3 origin = (camera.ViewInverse * vec4(0, 0, 0, 1)).xyz;
    vec3 target = (camera.ProjInverse * vec4(d.x, d.y, 1, 1)).xyz;
    vec3 direction = (camera.ViewInverse * vec4(normalize(target), 0)).xyz;

    float tmin = 0.00001;
    float tmax = 10000.0;

    return Ray(origin, tmin, direction, tmax);
}

Ray constructPrimaryRay(uvec2 pixel, uvec2 resolution, Camera camera)
{
    return constructPrimaryRay(pixel, resolution, camera, vec2(0.5f));
}

// TODO: Fix the `shadow terminator problem` properly
vec3 offsetRayOrigin(vec3 origin, vec3 normal)
{
    return origin + normal * 0.01f;
}
