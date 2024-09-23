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
};

cbuffer UboView : register(b0)
{
    SHADER_VARS ubo;
}

struct VOut
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

VOut main(OBJ_ATTRIBUTES input)
{
    VOut output;
    
    float4 worldPosition = float4(input.Position, 1.0f);
    float4 viewPosition = mul(ubo.viewMatrix, worldPosition);
    output.position = mul(ubo.projectionMatrix, viewPosition);
    
    // Simple color based on position for visualization
    output.color = float4(input.Position.xyz, 1.0f);
    
    return output;
}