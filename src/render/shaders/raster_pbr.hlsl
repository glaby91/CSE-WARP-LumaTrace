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

cbuffer AreaLightConstants : register(b2)
{
    float3 LightPosition; float LightIntensity;
    float3 LightColor; float LightRadius;
};

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
    const uint alphaMode = (uint)SurfaceFactors.w;
    if (alphaMode == 1 && base.a < SurfaceFactors.z) discard;
    if (DetailFactors.z > 0.5) { float3 glow=float3(12.0, 9.0, 5.5); return float4(pow(glow/(1.0+glow),1.0/2.2),1.0); }

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

    uint triangleId = TriangleBase + primitiveId;
    float4 triLight = TriangleLighting[triangleId];
    float3 viewDirection = normalize(CameraPosition.xyz - input.worldPosition);
    float3 toLight = LightPosition - input.worldPosition;
    float lightDistance = max(length(toLight), 1.e-4);
    float3 lightDirection = toLight / lightDistance;
    float ndotl = saturate(dot(n, lightDirection));
    float3 fillDirection = normalize(float3(0.65, 0.35, -0.67));
    float fillNdotL = saturate(dot(n, fillDirection));
    float3 fill = fillNdotL * float3(0.04, 0.055, 0.065);
    float3 diffuse = base.rgb * (0.32 + ndotl * triLight.rgb + fill) * (1.0 - metallic) * ao;
    float3 halfVector = normalize(lightDirection + viewDirection);
    float specularPower = lerp(96.0, 4.0, roughness);
    float3 specular = lerp(float3(0.04, 0.04, 0.04), base.rgb, metallic)
                    * pow(saturate(dot(n, halfVector)), specularPower)
                    * lerp(1.0, 0.18, roughness) * LightColor * LightIntensity
                    / max(lightDistance * lightDistance, 0.25);
    float3 color = diffuse + specular + emissive
                 + triLight.a * metallic * base.rgb * (1.0 - roughness) * 0.25;
    color = pow(saturate(color / (1.0 + color)), 1.0 / 2.2);
    return float4(color, alphaMode == 2 ? base.a : 1.0);
}
