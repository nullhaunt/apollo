struct VertexInput
{
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct DrawConstants
{
    float4x4 clipFromModel;
    int4 mode;
    float4 colors[3];
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> Draw;
Texture2D HeadTexture : register(t0);
SamplerState HeadSampler : register(s1);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(Draw.clipFromModel, float4(input.position.xyz, 1.0));
    output.uv = input.uv;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
    float4 texel = float4(1.0, 1.0, 1.0, 1.0);
    if (Draw.mode.x != 0)
    {
        texel = HeadTexture.Sample(HeadSampler, input.uv);
    }

    float4 color;
    if (Draw.mode.x == 0)
    {
        color = float4(Draw.colors[0].rgb, 1.0);
    }
    else if (Draw.mode.x == 1)
    {
        color = texel;
    }
    else if (Draw.mode.x == 2)
    {
        color = float4(Draw.colors[0].rgb * texel.r + Draw.colors[1].rgb * texel.g +
                       Draw.colors[2].rgb * texel.b, texel.a);
    }
    else if (Draw.mode.x == 3)
    {
        color = float4(Draw.colors[0].rgb, texel.r);
    }
    else if (Draw.mode.x == 4)
    {
        color = float4(Draw.colors[0].rgb * texel.r, 1.0);
    }
    else if (Draw.mode.x == 5)
    {
        color = float4(Draw.colors[0].rgb * texel.r, pow(texel.g, Draw.colors[1].g));
    }
    else
    {
        color = float4(1.0, 0.0, 1.0, 1.0);
    }

    clip(color.a - 0.0001);
    return color;
}
