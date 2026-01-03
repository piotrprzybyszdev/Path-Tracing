#include "common.glsl"

const float DirectionalLightDistance = 100000.0f;

float GGXDistribution(vec3 H, float alpha)
{
    const float Hx2 = H.x * H.x;
    const float Hy2 = H.y * H.y;
    const float Hz2 = H.z * H.z;

    const float alpha2 = alpha * alpha;

    const float denom = PI * alpha2 * pow(Hx2 / alpha2 + Hy2 / alpha2 + Hz2, 2.0f);

    return 1.0f / max(denom, 1.0f);
}

float Lambda(vec3 V, float alpha)
{
    const float Vx2 = V.x * V.x;
    const float Vy2 = V.y * V.y;
    const float Vz2 = abs(V.z) * abs(V.z);

    const float alpha2 = alpha * alpha;

    const float nom = sqrt(1.0f + (alpha2 * Vx2 + alpha2 * Vy2) / Vz2) - 1.0f;

    return nom / 2.0f;
}

float GGXSmith(vec3 V, float alpha)
{
    return 1.0f / (1.0f + Lambda(V, alpha));
}

float DielectricFresnel(float VdotH, float eta)
{
    float cosThetaI = VdotH;
    float sinThetaT2 = eta * eta * (1.0f - cosThetaI * cosThetaI);

    if (sinThetaT2 > 1.0)
        return 1.0;
    
    const float cosThetaT = sqrt(max(1.0 - sinThetaT2, 0.0));
    
    const float rs = (eta * cosThetaT - cosThetaI) / (eta * cosThetaT + cosThetaI);
    const float rp = (eta * cosThetaI - cosThetaT) / (eta * cosThetaI + cosThetaT);

    return (rs * rs + rp * rp) / 2.0f;
}

float SchlickFresnel(float VdotH)
{
    return pow(clamp(1.0 - VdotH, 0.0, 1.0), 5);
}
