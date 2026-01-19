#include "Common.hlsli"
#include "Transforms.hlsli"
#include "Color.hlsli"

struct Parameters {
    int input_descriptor;
    int output_descriptor;
};

ConstantBuffer<Parameters> g_parameters: register(b0);
SamplerState g_sampler_linear_clamp: register(s0);

[numthreads(8, 8, 1)]
void main(in uint3 thread_id: SV_DispatchThreadID)
{
    TextureCube<float4> input = ResourceDescriptorHeap[g_parameters.input_descriptor];
    RWTexture2D<float> output = ResourceDescriptorHeap[g_parameters.output_descriptor];

    uint2 output_resolution;
    output.GetDimensions(output_resolution.x, output_resolution.y);
    
    int2 pixel = thread_id.xy;

    if (any(pixel >= output_resolution)) {
        return;
    }

    float2 uv = PixelToUV(pixel, output_resolution);
    float3 direction = SquareToSphere(UvToUnitSquare(uv));

    uint2 input_size;
    uint input_mips;
    input.GetDimensions(0, input_size.x, input_size.y, input_mips);
    
    float mip_level = clamp(log2((6 * input_size.x) / output_resolution.x), 0, input_mips);

    float3 color = input.SampleLevel(g_sampler_linear_clamp, direction, mip_level).rgb;
    float luminance = Luminance(color);
    output[pixel] = luminance;
}