#include "common.glsl"

const float DirectionalLightDistance = 100000.0f;

float GGXDistribution(vec3 H, float alpha)
{
    const float Hx2 = H.x * H.x;
    const float Hy2 = H.y * H.y;
    const float Hz2 = H.z * H.z;

    const float alpha2 = alpha * alpha;

    const float denom = PI * alpha2 * pow(Hx2 / alpha2 + Hy2 / alpha2 + Hz2, 2.0f);

    return 1.0f / denom;
}

float Lambda(vec3 V, float alpha)
{
    const float Vx2 = V.x * V.x;
    const float Vy2 = V.y * V.y;
    const float Vz2 = abs(V.z) * abs(V.z);

    const float alpha2 = alpha * alpha;

    const float nom = -1.0f + sqrt(1.0f + (alpha2 * Vx2 + alpha2 * Vy2) / Vz2);

    return nom / 2.0f;
}

float GGXSmith(vec3 V, float alpha)
{
    return 1.0f / (1.0f + Lambda(V, alpha));
}

float SchlickFresnel(float VdotH)
{
    float m = clamp(1.0 - VdotH, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}
