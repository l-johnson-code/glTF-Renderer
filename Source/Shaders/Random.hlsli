#pragma once

// Stateless random number generator from https://jcgt.org/published/0009/03/02/
uint3 pcg3d(uint3 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

uint4 pcg4d(uint4 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    v ^= v >> 16u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v;
}

// Generates 3 random floats in the range [0, 1] using a uint3 seed.
float3 RandomFloat3(uint3 v)
{
    uint3 random = pcg3d(v);
    return (float3)random / float(0xffffffff);
}

// Generates 2 random floats in the range [0, 1] using a uint3 seed.
float2 RandomFloat2(uint3 v)
{
    return RandomFloat3(v).xy;
}

// Adapted from https://graphics.pixar.com/library/MultiJitteredSampling/paper.pdf.
// l must be a power of 2.
unsigned int Permute(unsigned int i, unsigned int l, unsigned int p)
{
    unsigned int w = l - 1;
    i ^= p;
    i *= 0xe170893d;
    i ^= p >> 16;
    i ^= (i & w) >> 4;
    i ^= p >> 8;
    i *= 0x0929eb3f;
    i ^= p >> 23;
    i ^= (i & w) >> 1;
    i *= 1 | p >> 27;
    i *= 0x6935fa69;
    i ^= (i & w) >> 11;
    i *= 0x74dcb303;
    i ^= (i & w) >> 2;
    i *= 0x9e501cc3;
    i ^= (i & w) >> 2;
    i *= 0xc860a3df;
    i &= w;
    i ^= i >> 5;
    return (i + p) & w;
}

// See https://extremelearning.com.au/unreasonable-effectiveness-of-quasirandom-sequences/ for more information about these R sequences.
// Note: This function will have precision issues for large values of n.
float R1(float start, int n)
{
    const float GOLDEN_RATIO_FRACTION = 0.618033988749894;
    return frac(start + n * GOLDEN_RATIO_FRACTION);
}

// Note: This function will have precision issues for large values of n.
float2 R2(float2 start, int n)
{
    const float g = 1.324717957244746;
    const float gg = g*g;
    return frac(start + n * float2(1./g, 1./gg));
}

float RadicalInverse(uint n)
{
    // Reverse bits.
    n = (n << 16u) | (n >> 16u);
    n = ((n & 0x00ff00ffu) << 8u) | ((n & 0xff00ff00u) >> 8u);
    n = ((n & 0x0f0f0f0fu) << 4u) | ((n & 0xf0f0f0f0u) >> 4u);
    n = ((n & 0x33333333u) << 2u) | ((n & 0xccccccccu) >> 2u);
    n = ((n & 0x55555555u) << 1u) | ((n & 0xaaaaaaaau) >> 1u);
    
    // Divide by 2^32.
    return (float)n * 2.3283064365386963e-10;
}

float2 Hammersley2d(int i, int n)
{
    return float2((float)i / (float)n, RadicalInverse(i));
}