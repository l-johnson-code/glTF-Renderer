#include "Common.hlsli"
#include "Random.hlsli"
#include "Color.hlsli"

struct PSIn {
    float4 pos: SV_POSITION;
    float2 uv: TEXCOORD;
};

struct PSOut {
    float4 color: SV_TARGET0;
};

enum Tonemapper {
    TONEMAPPER_NONE,
    TONEMAPPER_AGX,
};

struct Config {
    int tonemapper;
    float exposure;
    int frame;
};

RWTexture2D<float4> g_input: register(u0);
ConstantBuffer<Config> g_config: register(b0);

// AgX lookup table approximation by Benjamin Wrensch.
// https://iolite-engine.com/blog_posts/minimal_agx_implementation
float3 AgxCurve(float3 x)
{
    const float3 x_2 = x * x;
    const float3 x_4 = x_2 * x_2;

    float3 result = 15.5 * x_4 * x_2;
    result -= 40.14 * x_4 * x;
    result += 31.96 * x_4;
    result -= 6.868 * x_2 * x;
    result += 0.4298 * x_2;
    result += 0.1191 * x;
    result -= 0.00232;

    return result;
}

// Based on original AgX by Troy James Sobotka.
// https://github.com/sobotka/AgX/tree/main
// TODO: Implement EaryChow's version of AgX.
float3 AgxTonemap(float3 color)
{
    const float3x3 agx_inset = transpose(float3x3(
        0.856627153315983, 0.137318972929847, 0.11189821299995,
        0.0951212405381588, 0.761241990602591, 0.0767994186031903,
        0.0482516061458583, 0.101439036467562, 0.811302368396859
    ));
    color = mul(agx_inset, color);

    const float log_min = -12.47393;
    const float log_max = 4.026069;
    color = clamp(log2(color), log_min, log_max);
    color = (color - log_min) / (log_max - log_min);

    // Apply tonemap curve.
    color = AgxCurve(color);

    float3x3 agx_outset = transpose(float3x3(
        1.12710058, -0.14132976, -0.14132976,
        -0.11060664,  1.1578237,  -0.11060664,
        -0.01649394, -0.01649394,  1.25193641
    ));
    color = mul(agx_outset, color);
    color = pow(color, 2.2);
    
    return color;
}

float3 Dither(float3 input, uint3 seed)
{
    float3 triangle_noise = (RandomFloat3(seed * 2) + RandomFloat3(seed * 2 + 1) - 1.0);
    return input + (triangle_noise / 255.);
}

PSOut main(PSIn input)
{
    PSOut output;
    uint2 resolution;
    g_input.GetDimensions(resolution.x, resolution.y);
    uint2 pixel = UVToPixel(input.uv, resolution);
    float3 color = g_config.exposure * g_input[pixel].rgb;
    switch (g_config.tonemapper) {
        case TONEMAPPER_NONE: {
            color = saturate(color);
        } break;
        case TONEMAPPER_AGX: {
            color = AgxTonemap(color);
        } break;
    }
    color = EncodeSrgb(color);
    color = Dither(color, uint3(pixel.x, pixel.y, g_config.frame));
    output.color.rgb = color;
    return output;
}