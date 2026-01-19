#pragma once

// Calculate luminance using sRGB primaries.
float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 EncodeSrgb(float3 linear_color)
{
    float3 srgb = select(
        linear_color <= 0.0031308, 
        linear_color * 12.92, 
        1.055 * pow(linear_color, 1. / 2.4) - 0.055
    );
    return srgb;
}