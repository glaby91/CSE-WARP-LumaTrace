struct Triangle
{
    float3 p0; uint material;
    float3 p1; uint triangleId;
    float3 p2; uint pad0;
    float3 n0; uint pad1;
    float3 n1; uint pad2;
    float3 n2; uint pad3;
    float2 uv0; float2 uv1;
    float2 uv2; float2 pad4;
};
struct BvhNode
{
    float3 minimum; uint child0;
    float3 maximum; uint child1;
    uint child2; uint child3;
    uint firstTriangle; uint triangleCount;
    uint childCount; uint padding;
};
struct MaterialData { float4 baseColor; float metallic; float roughness; float2 pad; };

Texture2D<uint> VisibleTriangleIds : register(t0);
StructuredBuffer<Triangle> Triangles : register(t1);
StructuredBuffer<uint> TriangleOrder : register(t2);
StructuredBuffer<BvhNode> Bvh : register(t3);
StructuredBuffer<MaterialData> Materials : register(t4);
StructuredBuffer<uint> VisibleFlags : register(t5);
Texture2DArray<float4> BaseColorMaps : register(t6);
SamplerState MaterialSampler : register(s0);
RWStructuredBuffer<uint> OutVisibleFlags : register(u0);
RWStructuredBuffer<float4> TriangleRadiance : register(u1);

cbuffer LightConstants : register(b0)
{
    uint ImageWidth;
    uint ImageHeight;
    uint TriangleCount;
    uint SamplesPerTriangle; // clamp to [1,4]
    float3 CameraPosition;
    float RayEpsilon;
    float3 LightPosition;
    float KeyLightIntensity;
    float3 KeyLightColor;
    float LightAngularRadius;
};

[numthreads(8,8,1)]
void CSCollectVisible(uint3 dispatchId : SV_DispatchThreadID)
{
    if (dispatchId.x >= ImageWidth || dispatchId.y >= ImageHeight) return;
    uint id = VisibleTriangleIds.Load(int3(dispatchId.xy, 0));
    if (id >= TriangleCount) return;
    InterlockedOr(OutVisibleFlags[id], 1);
}

bool HitBounds(BvhNode node, float3 origin, float3 inverseDirection, float maxDistance)
{
    float3 a = (node.minimum - origin) * inverseDirection;
    float3 b = (node.maximum - origin) * inverseDirection;
    float3 lo = min(a, b), hi = max(a, b);
    float nearT = max(max(lo.x, lo.y), max(lo.z, 0.0));
    float farT = min(min(hi.x, hi.y), min(hi.z, maxDistance));
    return farT >= nearT;
}

bool HitTriangle(Triangle tri, float3 origin, float3 direction, float maxDistance,
                 out float hitT, out float3 barycentrics)
{
    barycentrics = 0.0;
    float3 e1 = tri.p1 - tri.p0, e2 = tri.p2 - tri.p0;
    float3 p = cross(direction, e2);
    float determinant = dot(e1, p);
    if (abs(determinant) < 1.e-8) { hitT = maxDistance; return false; }
    float invDet = 1.0 / determinant;
    float3 tvec = origin - tri.p0;
    float u = dot(tvec, p) * invDet;
    if (u < 0.0 || u > 1.0) { hitT = maxDistance; return false; }
    float3 q = cross(tvec, e1);
    float v = dot(direction, q) * invDet;
    if (v < 0.0 || u + v > 1.0) { hitT = maxDistance; return false; }
    hitT = dot(e2, q) * invDet;
    if (hitT <= RayEpsilon || hitT >= maxDistance) return false;
    barycentrics = float3(1.0 - u - v, u, v);
    return true;
}

bool TraceScene(float3 origin, float3 direction, float maxDistance, uint sourceTriangle,
                out uint hitTriangle, out float hitDistance, out float3 hitBarycentrics)
{
    uint stack[64]; uint top = 0; stack[top++] = 0;
    float3 safeDirection = float3(
        abs(direction.x) < 1.e-8 ? (direction.x < 0.0 ? -1.e-8 : 1.e-8) : direction.x,
        abs(direction.y) < 1.e-8 ? (direction.y < 0.0 ? -1.e-8 : 1.e-8) : direction.y,
        abs(direction.z) < 1.e-8 ? (direction.z < 0.0 ? -1.e-8 : 1.e-8) : direction.z);
    float3 invDir = rcp(safeDirection);
    hitTriangle = 0xffffffff; hitDistance = maxDistance; hitBarycentrics = 0.0;
    [loop] while (top > 0)
    {
        uint nodeIndex = stack[--top];
        BvhNode node = Bvh[nodeIndex];
        if (!HitBounds(node, origin, invDir, hitDistance)) continue;
        if (node.triangleCount > 0)
        {
            [loop] for (uint j = 0; j < node.triangleCount; ++j)
            {
                uint id = TriangleOrder[node.firstTriangle + j];
                if (id == sourceTriangle) continue;
                float t; float3 barycentrics;
                if (HitTriangle(Triangles[id], origin, direction, hitDistance, t, barycentrics))
                { hitDistance = t; hitTriangle = id; hitBarycentrics = barycentrics; }
            }
        }
        else if (node.childCount > 0 && top + node.childCount <= 64)
        {
            [unroll] for (uint child = 0; child < 4; ++child)
            {
                if (child < node.childCount)
                {
                    uint childIndex = child == 0 ? node.child0 :
                                      child == 1 ? node.child1 :
                                      child == 2 ? node.child2 : node.child3;
                    stack[top++] = childIndex;
                }
            }
        }
    }
    return hitTriangle != 0xffffffff;
}

float Hash1(uint v)
{
    v ^= v >> 16; v *= 0x7feb352d; v ^= v >> 15; v *= 0x846ca68b; v ^= v >> 16;
    return (v & 0x00ffffff) / 16777216.0;
}

float3 SamplePoint(Triangle t, uint sampleIndex, uint sampleCount)
{
    if (sampleCount <= 1) return (t.p0 + t.p1 + t.p2) / 3.0;
    float a = sampleIndex == 0 ? 0.64 : sampleIndex == 1 ? 0.18 : sampleIndex == 2 ? 0.18 : 0.34;
    float b = sampleIndex == 0 ? 0.18 : sampleIndex == 1 ? 0.64 : sampleIndex == 2 ? 0.18 : 0.33;
    float c = 1.0 - a - b;
    return t.p0*a + t.p1*b + t.p2*c;
}

float2 SampleAreaLight(uint sampleIndex, uint sampleCount)
{
    if (sampleCount <= 1) return float2(0.0, 0.0);
    if (sampleCount == 2) return sampleIndex == 0 ? float2(-0.55, 0.0) : float2(0.55, 0.0);
    // Four fixed, balanced samples keep the 1/2/4 comparisons reproducible.
    if (sampleIndex == 0) return float2(-0.5, -0.5);
    if (sampleIndex == 1) return float2( 0.5, -0.5);
    if (sampleIndex == 2) return float2(-0.5,  0.5);
    return float2(0.5, 0.5);
}

[numthreads(64,1,1)]
void CSShadeVisibleTriangles(uint3 dispatchId : SV_DispatchThreadID)
{
    uint triangleId = dispatchId.x;
    if (triangleId >= TriangleCount || VisibleFlags[triangleId] == 0) return;
    Triangle tri = Triangles[triangleId];
    uint samples = clamp(SamplesPerTriangle, 1, 4);
    float3 accumulatedDiffuse = 0.0;
    float3 accumulatedReflection = 0.0;
    [loop] for (uint sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
    {
        float3 p = SamplePoint(tri, sampleIndex, samples);
        float3 n = normalize(tri.n0 + tri.n1 + tri.n2);
        float3 toCenter = LightPosition - p;
        float centerDistance = max(length(toCenter), 1.e-4);
        float3 lightDirection = toCenter / centerDistance;
        float3 lightRight = normalize(cross(lightDirection, float3(0.0, 1.0, 0.0)));
        float3 lightUp = normalize(cross(lightDirection, lightRight));
        float2 lightSample = SampleAreaLight(sampleIndex, samples);
        float3 sampledPosition = LightPosition + LightAngularRadius *
            (lightRight * lightSample.x + lightUp * lightSample.y);
        float3 toLight = sampledPosition - p;
        float lightDistance = length(toLight);
        toLight /= max(lightDistance, 1.e-4);
        uint shadowHit; float shadowT; float3 shadowBarycentrics;
        bool occluded = TraceScene(p + n*RayEpsilon, toLight, max(RayEpsilon,lightDistance-RayEpsilon), triangleId,
                                   shadowHit, shadowT, shadowBarycentrics);
        float visible = occluded ? 0.0 : 1.0;
        // Raster applies its per-pixel Lambert term; compute contributes shadowed
        // inverse-square point-light radiance only.
        accumulatedDiffuse += visible * KeyLightIntensity * KeyLightColor
                            / max(lightDistance * lightDistance, 0.25);

        float3 viewDirection = normalize(CameraPosition - p);
        float3 reflectionDirection = reflect(-viewDirection,n);
        uint reflectionHit; float reflectionT; float3 reflectionBarycentrics;
        if (TraceScene(p+n*RayEpsilon,reflectionDirection,10000.0,triangleId,
                       reflectionHit,reflectionT,reflectionBarycentrics))
        {
            Triangle hit = Triangles[reflectionHit];
            MaterialData hitMaterial = Materials[hit.material];
            float2 hitUv = hit.uv0 * reflectionBarycentrics.x
                         + hit.uv1 * reflectionBarycentrics.y
                         + hit.uv2 * reflectionBarycentrics.z;
            float3 texturedBaseColor = BaseColorMaps
                                     .SampleLevel(MaterialSampler, float3(hitUv, float(hit.material)), 0.0).rgb;
            accumulatedReflection += hitMaterial.baseColor.rgb * texturedBaseColor;
        }
        else
        {
            float sky = saturate(reflectionDirection.y*0.5+0.5);
            accumulatedReflection += lerp(float3(0.08,0.09,0.11),float3(0.46,0.54,0.66),sky);
        }
    }
    float invSamples = rcp(float(samples));
    TriangleRadiance[triangleId] = float4(accumulatedDiffuse*invSamples,
                                          dot(accumulatedReflection,float3(0.2126,0.7152,0.0722))*invSamples);
}
