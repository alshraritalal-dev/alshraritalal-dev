Texture2D<float4> HdrTex : register(t0);

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOutput FullscreenVS(uint id : SV_VertexID)
{
    VSOutput output;
    const float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };
    output.position = float4(positions[id], 0.0f, 1.0f);
    output.uv = float2((positions[id].x + 1.0f) * 0.5f, 1.0f - (positions[id].y + 1.0f) * 0.5f);
    return output;
}

float3 AcesFitted(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 LinearToSrgb(float3 color)
{
    return pow(saturate(color), 1.0f / 2.2f);
}

float4 TonemapPS(VSOutput input) : SV_Target0
{
    const int3 pixel = int3(input.position.xy, 0);
    const float3 hdr = HdrTex.Load(pixel).rgb;
    const float3 mapped = LinearToSrgb(AcesFitted(hdr));
    return float4(mapped, 1.0f);
}
