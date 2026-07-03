struct VsIn {
    float3 position : POSITION;
    float4 color : COLOR;
};

struct VsOut {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

struct Constants {
    float4x4 world_to_clip;
};

ConstantBuffer<Constants> g_constants;

VsOut main(VsIn input)
{
    VsOut output;
    output.position = mul(g_constants.world_to_clip, float4(input.position, 1.0f));
    output.color = input.color;
    return output;
}