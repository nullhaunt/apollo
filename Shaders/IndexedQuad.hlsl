struct VertexInput
{
    float2 position : POSITION;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
    float2 uv : TEXCOORD0;
};

Texture2D QuadTexture : register(t0);
SamplerState QuadSampler : register(s1);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 0.5, 1.0);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    return float4(input.color, 1.0) * QuadTexture.Sample(QuadSampler, input.uv);
}
