#include "Common.hlsli"

struct Parameters {
    int input_descriptor;
    int output_descriptor;
    int sample_pattern;
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

    switch (g_parameters.sample_pattern) {
        case 1: {
            // This is the downsample filter from "Bandwidth-Efficient Rendering" by Marius Bjørge.
            float3 result = 0.5 * input_texture.Sample(g_sampler_linear_clamp, uv_centre);
            result += 0.125 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + offset_size);
            result += 0.125 * input_texture.Sample(g_sampler_linear_clamp, uv_centre - offset_size);
            result += 0.125 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(-offset_size.x, offset_size.y));
            result += 0.125 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(offset_size.x, -offset_size.y));
            output_texture[pixel] = result;
        } break;
        case 2: {
            // This is the downsample filter from "Next Generation Post Processing in Call of Duty Advanced Warfare" by Jorge Jimenez.
            float3 result = 0.5 * input_texture.Sample(g_sampler_linear_clamp, uv_centre);
            result += 0.5 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(offset_size.x, offset_size.y));
            result += 0.5 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(offset_size.x, -offset_size.y));
            result += 0.5 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(-offset_size.x, offset_size.y));
            result += 0.5 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + float2(offset_size.x, -offset_size.y));
            result += 0.25 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + 2 * float2(offset_size.x, 0));
            result += 0.25 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + 2 * float2(-offset_size.x, 0));
            result += 0.25 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + 2 * float2(0, offset_size.y));
            result += 0.25 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + 2 * float2(0, -offset_size.y));
            result += 0.125 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + 2 * float2(offset_size.x, offset_size.y));
            result += 0.125 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + 2 * float2(offset_size.x, -offset_size.y));
            result += 0.125 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + 2 * float2(-offset_size.x, offset_size.y));
            result += 0.125 * input_texture.Sample(g_sampler_linear_clamp, uv_centre + 2 * float2(offset_size.x, -offset_size.y));
            output_texture[pixel] = result / 4;
        } break;
        default: {
            float3 result = input_texture.Sample(g_sampler_linear_clamp, uv_centre);
            output_texture[pixel] = result;
        } break;
    }
}