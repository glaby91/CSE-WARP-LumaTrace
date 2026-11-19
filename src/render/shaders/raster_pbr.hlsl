cbuffer CameraConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    row_major float4x4 World;
    row_major float4x4 NormalWorld;
    float4 CameraPosition;
};

cbuffer MaterialConstants : register(b1)
{
    float4 BaseColorFactor;
    float4 SurfaceFactors; // metallic, roughness, alpha cutoff, alpha mode (0 opaque, 1 mask, 2 blend)
    float4 EmissiveFactor;
    float4 DetailFactors; // normal scale, occlusion strength, reserved, reserved
    uint TriangleBase;
    uint3 MaterialPadding;
};

struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; float4 tangent : TANGENT; float4 color : COLOR0; };
struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 color : COLOR0;
};

Texture2D<float4> BaseColorMap : register(t0);
Texture2D<float4> NormalMap : register(t1);
Texture2D<float4> MetallicRoughnessMap : register(t2);
Texture2D<float4> OcclusionMap : register(t3);
Texture2D<float4> EmissiveMap : register(t4);
StructuredBuffer<float4> TriangleLighting : register(t5);
SamplerState MaterialSampler : register(s0);

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPosition = mul(float4(input.position, 1.0), World);
    output.worldPosition = worldPosition.xyz;
    output.position = mul(worldPosition, ViewProjection);
    output.normal = normalize(mul(float4(input.normal, 0.0), NormalWorld).xyz);
    output.uv = input.uv;
    output.tangent = float4(normalize(mul(float4(input.tangent.xyz, 0.0), World).xyz), input.tangent.w);
    output.color = input.color;
    return output;
}

float3 ApplyNormalMap(float3 n, float3 tangent, float3 mapped)
{
    if (dot(tangent, tangent) < 1.e-6) return n;
    float3 t = normalize(tangent - n * dot(n, tangent));
    float3 b = normalize(cross(n, t));
    return normalize(mapped.x * t + mapped.y * b + mapped.z * n);
}

float4 PSMain(VSOutput input, uint primitiveId : SV_PrimitiveID) : SV_Target0
{
    float4 base = BaseColorMap.Sample(MaterialSampler, input.uv) * BaseColorFactor * input.color;
    // Direct-lit non-emissive racing red paint override for the Porsche body.
    if (MaterialPadding.x != 0) base.rgb = float3(1.00, 0.008, 0.020);
    const uint alphaMode = (uint)SurfaceFactors.w;
    if (alphaMode == 1 && base.a < SurfaceFactors.z) discard;

    float4 mr = MetallicRoughnessMap.Sample(MaterialSampler, input.uv);
    float metallic = saturate(SurfaceFactors.x * mr.b);
    float roughness = clamp(SurfaceFactors.y * mr.g, 0.035, 1.0);
    float3 n = normalize(input.normal);
    float3 tangent = input.tangent.xyz;
    if (dot(tangent, tangent) < 1.e-6)
    {
        float3 dpdx = ddx(input.worldPosition), dpdy = ddy(input.worldPosition);
        float2 duvdx = ddx(input.uv), duvdy = ddy(input.uv);
        tangent = normalize(dpdx * duvdy.y - dpdy * duvdx.y);
    }
    float3 normalSample = NormalMap.Sample(MaterialSampler, input.uv).xyz * 2.0 - 1.0;
    normalSample.xy *= DetailFactors.x;
    n = ApplyNormalMap(n, tangent, normalSample);
    float ao = lerp(1.0, OcclusionMap.Sample(MaterialSampler, input.uv).r, saturate(DetailFactors.y));
    float3 emissive = EmissiveMap.Sample(MaterialSampler, input.uv).rgb * EmissiveFactor.rgb;

    // World-space side graphics: an ivory shoulder pinstripe and graphite/silver
    // diagonal sill bands. These are albedo, not emissive paint.
    if (MaterialPadding.x != 0)
    {
        float sideFacing = smoothstep(0.42, 0.78, abs(normalize(input.normal).x));
        float z = input.worldPosition.z;
        float y = input.worldPosition.y;
        float bodySpan = smoothstep(-2.62, -2.42, z) * (1.0 - smoothstep(2.32, 2.52, z));
        float pinLine = 1.0 - smoothstep(0.018, 0.038, abs(y - (1.17 + 0.025 * sin(z * 1.2))));
        float slashCoord = y - (0.48 + 0.105 * (z + 2.4));
        float graphiteBand = 1.0 - smoothstep(0.045, 0.075, abs(slashCoord));
        float silverBand = 1.0 - smoothstep(0.026, 0.048, abs(slashCoord - 0.085));
        float graphics = sideFacing * bodySpan;
        base.rgb = lerp(base.rgb, float3(0.055, 0.060, 0.070), graphiteBand * graphics);
        base.rgb = lerp(base.rgb, float3(0.62, 0.63, 0.59), silverBand * graphics);
        base.rgb = lerp(base.rgb, float3(0.90, 0.84, 0.70), pinLine * graphics);
    }

    uint triangleId = TriangleBase + primitiveId;
    float4 triLight = TriangleLighting[triangleId];
    float3 viewDirection = normalize(CameraPosition.xyz - input.worldPosition);
    float3 keyDirection = normalize(float3(0.96, 0.24, 0.17));
    float ndotl = saturate(dot(n, keyDirection));
    float3 diffuse = base.rgb * (0.18 + ndotl * triLight.rgb) * (1.0 - metallic) * ao;
    float3 halfVector = normalize(keyDirection + viewDirection);
    float specularPower = lerp(96.0, 4.0, roughness);
    float3 specular = lerp(float3(0.04, 0.04, 0.04), base.rgb, metallic)
                    * pow(saturate(dot(n, halfVector)), specularPower)
                    * lerp(1.0, 0.18, roughness);
    float3 color = diffuse + specular + emissive + triLight.a * metallic * base.rgb * (1.0 - roughness) * 0.25;
    color = pow(saturate(color / (1.0 + color)), 1.0 / 2.2);
    return float4(color, alphaMode == 2 ? base.a : 1.0);
}
