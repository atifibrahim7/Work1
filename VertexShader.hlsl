// A simple HLSL vertex shader that processes 3D positions

float4 main(float3 inputVertex : POSITION) : SV_POSITION
{
    // Pass the 3D position directly to the output, setting w to 1 for homogeneous coordinates
    return float4(inputVertex, 1.0f);
}