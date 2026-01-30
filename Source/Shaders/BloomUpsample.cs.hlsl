#include "Common.hlsli"

struct Parameters {
    int input_descriptor;
    int output_descriptor;
    float input_scale;
    float output_scale;
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
    float2 offset_size = 1.0 / (float2)resolution;
    float2 uv_centre = PixelToUV(pixel, resolution);

    // This is the upsample filter from "Bandwidth-Efficient Rendering" by Marius Bjørge.
    float3 result = 0.0f.xxx;
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(offset_size.x, 0.0f));
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(-offset_size.x, 0.0f));
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(0.0f, offset_size.y));
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(0.0f, -offset_size.y));
    result *= 2.0f;
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(offset_size.x, offset_size.y));
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(-offset_size.x, offset_size.y));
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(offset_size.x, -offset_size.y));
    result += input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(-offset_size.x, -offset_size.y));
    result /= 12.0f;

    result = g_parameters.input_scale * result + g_parameters.output_scale * output_texture[pixel];

    output_texture[pixel] = result;
}