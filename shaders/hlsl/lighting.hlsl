struct CameraConstants
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 invViewProj;
    float3 worldPos;
    float nearPlane;
    float farPlane;
    float3 pad;
};

ConstantBuffer<CameraConstants> Camera : register(b0);
Texture2D<float4> AlbedoTex : register(t0);
Texture2D<float4> NormalTex : register(t1);
Texture2D<float4> MraoTex : register(t2);
Texture2D<float> DepthTex : register(t3);

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

float DistributionGGX(float3 n, float3 h, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float ndoth = max(dot(n, h), 0.0f);
    const float ndoth2 = ndoth * ndoth;
    const float denom = ndoth2 * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * denom * denom, 0.0001f);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    const float r = roughness + 1.0f;
    const float k = (r * r) / 8.0f;
    return ndotv / max(ndotv * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float3 n, float3 v, float3 l, float roughness)
{
    return GeometrySchlickGGX(max(dot(n, v), 0.0f), roughness) *
        GeometrySchlickGGX(max(dot(n, l), 0.0f), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 ReconstructWorld(float2 uv, float depth)
{
    const float4 clip = float4(uv * 2.0f - 1.0f, depth, 1.0f);
    float4 world = mul(clip, Camera.invViewProj);
    return world.xyz / max(world.w, 0.0001f);
}

float4 LightingPS(VSOutput input) : SV_Target0
{
    const int3 pixel = int3(input.position.xy, 0);
    const float4 albedoSample = AlbedoTex.Load(pixel);
    const float3 albedo = max(albedoSample.rgb, 0.0f);
    const float3 n = normalize(NormalTex.Load(pixel).xyz * 2.0f - 1.0f);
    const float4 mrao = MraoTex.Load(pixel);
    const float metallic = saturate(mrao.x);
    const float roughness = max(saturate(mrao.y), 0.04f);
    const float ao = saturate(mrao.z);
    const float depth = DepthTex.Load(pixel);
    const float3 world = ReconstructWorld(input.uv, depth);
    const float3 v = normalize(Camera.worldPos - world);
    const float3 l = normalize(float3(-0.5f, 1.0f, -0.5f));
    const float3 h = normalize(v + l);
    const float3 radiance = float3(1.0f, 1.0f, 1.0f) * 5.0f;

    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    const float d = DistributionGGX(n, h, roughness);
    const float g = GeometrySmith(n, v, l, roughness);
    const float3 f = FresnelSchlick(max(dot(h, v), 0.0f), f0);
    const float3 numerator = d * g * f;
    const float denom = max(4.0f * max(dot(n, v), 0.0f) * max(dot(n, l), 0.0f), 0.0001f);
    const float3 specular = numerator / denom;
    const float3 kd = (1.0f - f) * (1.0f - metallic);
    const float ndotl = max(dot(n, l), 0.0f);
    const float3 lo = (kd * albedo / 3.14159265f + specular) * radiance * ndotl;
    const float3 ambient = albedo * 0.18f * ao;
    return float4(ambient + lo, 1.0f);
}
