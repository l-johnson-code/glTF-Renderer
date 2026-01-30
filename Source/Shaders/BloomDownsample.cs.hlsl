#include "Common.hlsli"

struct Parameters {
    int input_descriptor;
    int output_descriptor;
};

ConstantBuffer<Parameters> g_parameters: register(b0);
SamplerState g_sampler_linear_clamp: register(s0);

[numthreads(8, 8, 1)]
void main(in uint3 dispatch_thread_id: SV_DispatchThreadID)
{
    Texture2D<float3> input_texture = ResourceDescriptorHeap[g_parameters.input_descriptor];
    RWTexture2D<float3> output_texture = ResourceDescriptorHeap[g_parameters.output_descriptor];

    uint2 resolution;
    output_texture.GetDimensions(resolution.x, resolution.y);
    
    if (any(dispatch_thread_id.xy >= resolution)) {
        return;
    }

    uint2 pixel = dispatch_thread_id.xy;

    // TODO: Consider using gather functions and then manually performing the bilinear sampling. Could potentially use less loads.
    // TODO: Consider storing samples in group shared memory to share sample results across pixels.
    float2 offset_size = 0.5 / (float2)resolution;
    float2 uv_centre = PixelToUV(pixel, resolution);

    // This is the downsample filter from "Bandwidth-Efficient Rendering" by Marius Bjørge.
    float3 result = 4.0f * input_texture.Sample(g_sampler_linear_clamp, uv_centre);
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + offset_size);
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre - offset_size);
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(-offset_size.x, offset_size.y));
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(offset_size.x, -offset_size.y));
    output_texture[pixel] = result / 8.0f;
}