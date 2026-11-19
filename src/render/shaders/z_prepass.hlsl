cbuffer CameraConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    row_major float4x4 World;
    uint TriangleBase;
    uint AlphaMode;
    float AlphaCutoff;
    float BaseColorAlpha;
};

struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; float4 tangent : TANGENT; float4 color : COLOR0; };
struct VSOutput { float4 position : SV_POSITION; float2 uv : TEXCOORD0; float vertexAlpha : COLOR0; };
Texture2D<float4> BaseColorMap : register(t0);
SamplerState MaterialSampler : register(s0);

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPosition = mul(float4(input.position, 1.0), World);
    output.position = mul(worldPosition, ViewProjection);
    output.uv = input.uv;
    output.vertexAlpha = input.color.a;
    return output;
}

uint PSMain(VSOutput input, uint primitiveId : SV_PrimitiveID) : SV_Target0
{
    if (AlphaMode == 1)
        clip(BaseColorMap.Sample(MaterialSampler, input.uv).a * BaseColorAlpha * input.vertexAlpha - AlphaCutoff);
    return TriangleBase + primitiveId;
}
