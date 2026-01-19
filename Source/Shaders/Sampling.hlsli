#pragma once

#include "Common.hlsli"
#include "Bsdf.hlsli"
#include "Transforms.hlsli"

float3 SampleHemisphere(float2 u) 
{
    float3 result;
    result.x = sqrt(1. - u.y * u.y) * cos(u.x * TAU),
    result.y = sqrt(1. - u.y * u.y) * sin(u.x * TAU),
    result.z = u.y;
    return result;
}

float3 SampleCosineWeightedHemisphere(float2 u)
{
    float3 result;
    result.xy = SquareToDisk2(UvToUnitSquare(u));
    result.z = sqrt(1 - result.x * result.x - result.y * result.y);
    return result;
}

// Generate a cosine weighted sample without using a local coordinate basis.
// https://fizzer.neocities.org/lambertnotangent
float3 SampleCosineWeightedHemisphere(float3 n, float2 u)
{
   float theta = TAU * u.x;
   u.y = 2 * u.y - 1;
   float3 sphere = float3(sqrt(1.0 - u.y * u.y) * float2(cos(theta), sin(theta)), u.y);
   float3 result = normalize(n + sphere);
   return result;
}

float CosineWeightedHemispherePdf(float3 n, float3 v)
{
    return saturate(dot(v, n) / PI);
}

// Samples the hemisphere with a pdf that matches the normal distribution function for ggx.
float3 SampleGgxNormal(float a, float2 u)
{
    float phi = TAU * u.x;
    float cos_theta = sqrt((1 - u.y) / (1 + (a * a - 1) * u.y));
    float sin_theta = sqrt(1 - cos_theta * cos_theta);
    
    float3 h;
    h.x = sin_theta * cos(phi); 
    h.y = sin_theta * sin(phi); 
    h.z = cos_theta;
    return h;
}

float GgxNormalPdf(float a, float3 n, float3 h)
{
    float n_dot_h = dot(n, h);
    return GgxD(a, n_dot_h) * n_dot_h;
}

float GgxVisibleNormalPdf(float a, float3 v, float3 h)
{
    if (v.z <= 0) {
        return 0;
    }
    float pdf = GgxD(a, h.z);
    pdf *= max(0, dot(v, h));
    pdf *= GgxSmithG1(a, v.z, dot(v, h));
    pdf /= v.z;
    return pdf;
}

float GgxVisibleNormalPdf(float2 a, float3 v, float3 h)
{
    if (v.z <= 0) {
        return 0;
    }
    float pdf = GgxAnisotropicD(a, h);
    pdf *= max(0, dot(v, h));
    pdf *= GgxAnisotropicSmithG1(a, v, dot(v, h));
    pdf /= v.z;
    return pdf;
}

// Sample visible GGX normals with spherical caps: https://arxiv.org/abs/2306.05044.
float3 SampleGgxVisibleNormal(float2 a, float3 v, float2 u)
{
    // Scale the view vector.
    float3 v_scaled = normalize(float3(a * v.xy, v.z));

    // Sample a spherical cap in (-wi.z, 1].
    float phi = TAU * u.x;
    float z = (1.0f - u.y) * (1.0f + v.z) - v.z;
    float sin_theta = sqrt(clamp(1.0f - z * z, 0.0f, 1.0f));
    float x = sin_theta * cos(phi);
    float y = sin_theta * sin(phi);
    float3 c = float3(x, y, z);
    float3 hemisphere_normal = c + v;

    // Transform normal back to ellipse.
    float3 h = normalize(float3(a * hemisphere_normal.xy, max(0.0, hemisphere_normal.z)));

    return h;
}

// Isotropic version.
float3 SampleGgxVisibleNormal(float a, float3 v, float2 u)
{
    return SampleGgxVisibleNormal(a.xx, v, u);
}

float2 SampleImportanceMap(Texture2D<float> texture, float2 u, out float pdf)
{
    uint width, height, mips;
    texture.GetDimensions(0, width, height, mips);
    pdf = 1;
    uint2 pixel = uint2(0, 0);
    for (int i = mips - 2; i >= 0; i--) {
        pixel <<= 1;
        float upper_left = texture.mips[i][pixel];
        float upper_right = texture.mips[i][pixel + uint2(1, 0)];
        float lower_left = texture.mips[i][pixel + uint2(0, 1)];
        float lower_right = texture.mips[i][pixel + uint2(1, 1)];
        float left = upper_left + lower_left;
        float right = upper_right + lower_right;
        float total = left + right;
        float prob_left = left / total;
        if (u.x < prob_left) {
            u.x /= prob_left;
            float prob_upper = upper_left / left;
            if (u.y < prob_upper) {
                u.y /= prob_upper;
            } else {
                pixel.y++;
                u.y = (u.y - prob_upper) / (1 - prob_upper);
            }
        } else {
            pixel.x++;
            u.x = (u.x - prob_left) / (1 - prob_left);
            float prob_upper = upper_right / right;
            if (u.y < prob_upper) {
                u.y /= prob_upper;
            } else {
                pixel.y++;
                u.y = (u.y - prob_upper) / (1 - prob_upper);
            }
        }
    }
    pdf = (float)width * (float)height * texture[pixel] / texture.mips[mips - 1][0.xx];
    float2 uv = ((float2)pixel + u) / (float2)width;
    return uv; 
}

float ImportanceMapPdf(Texture2D<float> texture, float2 uv)
{
    uint2 resolution = 0;
    uint mip_count = 0;
    texture.GetDimensions(0, resolution.x, resolution.y, mip_count);
    float total = texture.mips[mip_count - 1][0.xx];
    int2 pixel = UVToPixel(uv, resolution.xx);
    float value = texture.mips[0][pixel];
    return (float)resolution.x * (float)resolution.y * value / total;
}