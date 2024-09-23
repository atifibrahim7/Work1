struct OBJ_ATTRIBUTES
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float4 Tangent : TANGENT;
};

struct SHADER_VARS
{
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
    float4 sunDirection;
    float4 sunColor;
    float4 cameraPosition;
};

cbuffer UboView : register(b0)
{
    SHADER_VARS ubo;
}

struct VOut
{
    float4 position : SV_POSITION;
    float3 worldPos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

VOut main(OBJ_ATTRIBUTES input)
{
    VOut output;
    
    float4 worldPosition = float4(input.Position, 1.0f);
    float4 viewPosition = mul(ubo.viewMatrix, worldPosition);
    output.position = mul(ubo.projectionMatrix, viewPosition);
    
    output.worldPos = worldPosition.xyz;
    output.normal = input.Normal;
    output.uv = input.UV;
    output.tangent = input.Tangent;
    
    return output;
}