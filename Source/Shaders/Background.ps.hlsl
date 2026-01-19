struct Parameters {
    float environment_intensitity;
    int environment_descriptor;
};

struct PsIn {
    float4 pos: SV_POSITION;
    float3 direction: DIRECTION;
};

struct PsOut {
    float4 lighting: SV_TARGET0;
};

ConstantBuffer<Parameters> g_parameters: register(b0);
SamplerState g_sampler_linear_clamp: register(s0);

PsOut main(PsIn input)
{
    PsOut output;
    float3 direction = normalize(input.direction);
    TextureCube<float3> environment_cube = ResourceDescriptorHeap[g_parameters.environment_descriptor];
    output.lighting.rgb = g_parameters.environment_intensitity * environment_cube.Sample(g_sampler_linear_clamp, direction);
    output.lighting.a = 1.0;
    return output;
}