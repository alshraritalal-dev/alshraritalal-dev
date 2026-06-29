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

struct ObjectConstants
{
    float4x4 world;
    float4 baseColorMetallic;
    float4 roughnessEmissivePad;
};

ConstantBuffer<CameraConstants> Camera : register(b0);
ConstantBuffer<ObjectConstants> Object : register(b1);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 colorMetallic : TEXCOORD3;
    float4 roughnessEmissivePad : TEXCOORD4;
};

VSOutput SceneVS(VSInput input)
{
    VSOutput output;
    const float4 worldPosition = mul(float4(input.position, 1.0f), Object.world);
    output.position = mul(worldPosition, Camera.viewProj);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(mul(float4(input.normal, 0.0f), Object.world).xyz);
    output.uv = input.uv;
    output.colorMetallic = Object.baseColorMetallic;
    output.roughnessEmissivePad = Object.roughnessEmissivePad;
    return output;
}

struct GBufferOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 mrao : SV_Target2;
};

GBufferOutput GBufferPS(VSOutput input)
{
    GBufferOutput output;
    const float3 normal = normalize(input.normal) * 0.5f + 0.5f;
    output.albedo = float4(saturate(input.colorMetallic.rgb), 1.0f);
    output.normal = float4(normal, 1.0f);
    output.mrao = float4(input.colorMetallic.a, saturate(input.roughnessEmissivePad.x), 1.0f, 1.0f);
    return output;
}

float4 ForwardPS(VSOutput input) : SV_Target0
{
    const float3 n = normalize(input.normal);
    const float3 l = normalize(float3(0.5f, -1.0f, 0.5f));
    const float ndotl = saturate(dot(n, -l));
    const float3 ambient = input.colorMetallic.rgb * 0.06f;
    const float3 diffuse = input.colorMetallic.rgb * ndotl * 3.0f;
    return float4(ambient + diffuse, 1.0f);
}
