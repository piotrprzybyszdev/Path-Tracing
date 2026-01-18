void computeDpnDuv(Vertex v0, Vertex v1, Vertex v2, Vertex vertex, out vec3 dpdu, out vec3 dpdv, out vec3 dndu, out vec3 dndv)
{
    vec3 e1 = v1.Position - v0.Position;
    vec3 e2 = v2.Position - v0.Position;
    vec3 en1 = v1.Normal - v0.Normal;
    vec3 en2 = v2.Normal - v0.Normal;
    vec2 duv1 = v1.TexCoords - v0.TexCoords;
    vec2 duv2 = v2.TexCoords - v0.TexCoords;

    float det = duv1.x * duv2.y - duv2.x * duv1.y;

    if (abs(det) < 1e-8)
    {
        dpdu = vertex.Tangent;
        dpdv = vertex.Bitangent;
        dndu = vec3(0.0f);
        dndv = vec3(0.0f);
    }
    else
    {
        float invDet = 1.0f / det;
        dpdu = (duv2.y * e1 - duv1.y * e2) * invDet;
        dpdv = (-duv2.x * e1 + duv1.x * e2) * invDet;
        dndu = (duv2.y * en1 - duv1.y * en2) * invDet;
        dndv = (-duv2.x * en1 + duv1.x * en2) * invDet;
    }
}

void computeDpDxy(vec3 p, vec3 origin, vec3 direction, vec3 rxOrigin, vec3 rxDirection, vec3 ryOrigin, vec3 ryDirection, vec3 n, out vec3 dpdx, out vec3 dpdy)
{
    float d = -dot(n, p);
    float tx = (-dot(n, rxOrigin) - d) / dot(n, rxDirection);
    vec3 px = rxOrigin + tx * rxDirection;
    float ty = (-dot(n, ryOrigin) - d) / dot(n, ryDirection);
    vec3 py = ryOrigin + ty * ryDirection;

    dpdx = px - p;
    dpdy = py - p;
}

float differenceOfProducts(float a, float b, float c, float d)
{
    float cd = c * d;
    float differenceOfProducts = fma(a, b, -cd);
    float error = fma(-c, d, cd);
    return differenceOfProducts + error;
}

vec4 computeDerivatives(vec3 dpdx, vec3 dpdy, vec3 dpdu, vec3 dpdv)
{
    float ata00 = dot(dpdu, dpdu);
    float ata01 = dot(dpdu, dpdv);
    float ata11 = dot(dpdv, dpdv);

    float invDet = 1 / differenceOfProducts(ata00, ata11, ata01, ata01);
    invDet = isinf(invDet) ? 0.0f : invDet;

    float atb0x = dot(dpdu, dpdx);
    float atb1x = dot(dpdv, dpdx);
    float atb0y = dot(dpdu, dpdy);
    float atb1y = dot(dpdv, dpdy);

    float dudx = differenceOfProducts(ata11, atb0x, ata01, atb1x) * invDet;
    float dvdx = differenceOfProducts(ata00, atb1x, ata01, atb0x) * invDet;
    float dudy = differenceOfProducts(ata11, atb0y, ata01, atb1y) * invDet;
    float dvdy = differenceOfProducts(ata00, atb1y, ata01, atb0y) * invDet;

    dudx = isinf(dudx) ? 0.0f : clamp(dudx, -1e8f, 1e8f);
    dvdx = isinf(dvdx) ? 0.0f : clamp(dvdx, -1e8f, 1e8f);
    dudy = isinf(dudy) ? 0.0f : clamp(dudy, -1e8f, 1e8f);
    dvdy = isinf(dvdy) ? 0.0f : clamp(dvdy, -1e8f, 1e8f);

    return vec4(dudx, dvdx, dudy, dvdy);
}

void computeReflectedDifferentialRays(vec4 derivatives, vec3 n, vec3 p, vec3 viewDir, vec3 reflectedDir, vec3 dndu, vec3 dndv, inout vec3 rxOrigin, inout vec3 rxDirection, inout vec3 ryOrigin, inout vec3 ryDirection)
{
    float dudx = derivatives.x;
    float dvdx = derivatives.y;
    float dudy = derivatives.z;
    float dvdy = derivatives.w;

    vec3 dndx = dndu * dudx + dndv * dvdx;
    vec3 dndy = dndu * dudy + dndv * dvdy;

    float d = -dot(n, p);
    float tx = (-dot(n, rxOrigin) - d) / dot(n, rxDirection);
    vec3 px = rxOrigin + tx * rxDirection;
    float ty = (-dot(n, ryOrigin) - d) / dot(n, ryDirection);
    vec3 py = ryOrigin + ty * ryDirection;

    vec3 dwodx = -rxDirection - viewDir;
    vec3 dwody = -ryDirection - viewDir;

    rxOrigin = px;
    ryOrigin = py;

    float dwoDotn_dx = dot(dwodx, n) + dot(viewDir, dndx);
    float dwoDotn_dy = dot(dwody, n) + dot(viewDir, dndy);

    rxDirection = normalize(reflectedDir - dwodx + 2 * (dot(viewDir, n) * dndx + dwoDotn_dx * n));
    ryDirection = normalize(reflectedDir - dwody + 2 * (dot(viewDir, n) * dndy + dwoDotn_dy * n));
}

void computeRefractedDifferentialRays(vec4 derivatives, vec3 n, vec3 p, vec3 viewDir, vec3 refractedDir, vec3 dndu, vec3 dndv, float eta, inout vec3 rxOrigin, inout vec3 rxDirection, inout vec3 ryOrigin, inout vec3 ryDirection)
{
    float dudx = derivatives.x;
    float dvdx = derivatives.y;
    float dudy = derivatives.z;
    float dvdy = derivatives.w;

    vec3 dndx = dndu * dudx + dndv * dvdx;
    vec3 dndy = dndu * dudy + dndv * dvdy;

    float d = -dot(n, p);
    float tx = (-dot(n, rxOrigin) - d) / dot(n, rxDirection);
    vec3 px = rxOrigin + tx * rxDirection;
    float ty = (-dot(n, ryOrigin) - d) / dot(n, ryDirection);
    vec3 py = ryOrigin + ty * ryDirection;

    vec3 dwodx = -rxDirection - viewDir;
    vec3 dwody = -ryDirection - viewDir;

    rxOrigin = px;
    ryOrigin = py;

    if (dot(viewDir, n) < 0.0f)
    {
        n = -n;
        dndx = -dndx;
        dndy = -dndy;
    }

    float dwoDotn_dx = dot(dwodx, n) + dot(viewDir, dndx);
    float dwoDotn_dy = dot(dwody, n) + dot(viewDir, dndy);
    float mu = dot(viewDir, n) / eta - abs(dot(refractedDir, n));
    float dmudx = dwoDotn_dx * (1.0f / eta + 1.0f / (eta * eta) * dot(viewDir, n) / dot(refractedDir, n));
    float dmudy = dwoDotn_dy * (1.0f / eta + 1.0f / (eta * eta) * dot(viewDir, n) / dot(refractedDir, n));

    rxDirection = normalize(refractedDir - eta * dwodx + vec3(mu * dndx + dmudx * n));
    ryDirection = normalize(refractedDir - eta * dwody + vec3(mu * dndy + dmudy * n));
}

float computeLod(vec4 derivatives)
{
    float dudx = derivatives.x;
    float dvdx = derivatives.y;
    float dudy = derivatives.z;
    float dvdy = derivatives.w;
    float sx = sqrt(dudx * dudx + dvdx * dvdx);
    float sy = sqrt(dudy * dudy + dvdy * dvdy);
    float smax = max(sx, sy);
    return smax == 0.0f ? 0.0f : log2(smax);
}
