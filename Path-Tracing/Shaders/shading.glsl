#include "common.glsl"

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

vec3 EvaluateReflection(vec3 V, vec3 L, vec3 F, float alpha, out float pdf)
{
    if (L.z < 0.00001f)
    {
        pdf = 0.0f;
        return vec3(0.0f);
    }

    const vec3 H = normalize(V + L);

    const float LdotH = dot(L, H);
    const float VdotH = dot(V, H);

    const float D = GGXDistribution(H, alpha);

    const float GV = GGXSmith(V, alpha);
    const float GL = GGXSmith(L, alpha);
    const float G = GV * GL;

    pdf = (GV * max(VdotH, 0.0f) * D / V.z) / (4.0f * VdotH);
    return G * D * F / (4.0f * V.z);
}

vec3 EvaluateRefraction(vec3 V, vec3 L, vec3 F, float alpha, float eta, out float pdf)
{
    if (L.z >= 1e-5)
    {
        pdf = 0.0f;
        return vec3(0.0f);
    }

    vec3 H = normalize(eta * V + L);

    if (H.z < 0.0f)
        H = -H;
    
    const float VdotH = dot(V, H);
    const float LdotH = dot(L, H);

    const float D = GGXDistribution(H, alpha);
    const float GV = GGXSmith(V, alpha);
    const float GL = GGXSmith(L, alpha);
    const float G = GV * GL;

    const float denominator = LdotH + eta * VdotH;
    const float denominator2 = denominator * denominator;
    const float eta2 = eta * eta;

    const float jacobian = (eta2 * abs(LdotH)) / denominator2;

    pdf = (GV * abs(VdotH) * D / V.z) * jacobian;
    return (F * D * G * eta2 / denominator2) * (abs(VdotH) * abs(LdotH) / (abs(V.z)));
}

vec3 SampleGGX(vec2 u, vec3 V, float alpha)
{
    vec3 Vh = normalize(vec3(alpha * V.x, alpha * V.y, abs(V.z)));

    const float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    const vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * (1.0f / sqrt(lensq)) : vec3(1, 0, 0);
    const vec3 T2 = cross(Vh, T1);

    const float r = sqrt(u.x);
    const float phi = 2.0 * PI * u.y;
    const float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    const float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    const vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    return normalize(vec3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));
}