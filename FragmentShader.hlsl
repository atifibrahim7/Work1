struct PSInput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

float4 main(PSInput input) : SV_TARGET
{   
    // For now, just return a simple color
    return float4(1.0f, 0.0f, 1.0f, 1.0f);
}