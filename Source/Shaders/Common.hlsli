#pragma once

enum StaticDescriptor {
	STATIC_DESCRIPTOR_UAV_DISPLAY,
	STATIC_DESCRIPTOR_SRV_DEPTH,
	STATIC_DESCRIPTOR_SRV_MOTION_VECTORS,
	STATIC_DESCRIPTOR_SRV_SHEEN_E,
	STATIC_DESCRIPTOR_COUNT,
};

static const float PI = 3.14159265359;
static const float TAU = 2 * PI;

// Takes a uv coordinate and returns its corresponding pixel. 
int2 UVToPixel(float2 uv, int2 resolution)
{
    return (int2)(floor(uv * (float2)resolution) - .5);
}

// Takes a pixel and returns its corresponding uv coordinate.
float2 PixelToUV(int2 pixel, int2 resolution)
{
    return ((float2)pixel + .5) / (float2)resolution;
}

int2 NormalizeTexelCoordinate(float2 uv, int2 resolution)
{
    return (int2)((uv * (float2)resolution) - .5);
}

float2 UnnormalizeTexelCoordinate(float2 unormalized, int2 resolution)
{
    return unormalized * (float2)resolution;
}

void CreateBasis(float3 n, out float3 t, out float3 b)
{
    if (abs(n.x) > abs(n.z)) {
        b = float3(-n.y, n .x, 0);
    } else {
        b = float3(0, -n.z, n.y);
    }
    b = normalize(b);
    t = cross(b, n);
}

// A more accurate basis generation function.
// https://jcgt.org/published/0006/01/01/
void CreateBasisAccurate(float3 n, out float3 b1, out float3 b2)
{
    if (n.z < 0) {
        const float a = 1.0f / (1.0f - n.z);
        const float b = n.x * n.y * a;
        b1 = float3(1.0f - n.x * n.x * a, -b, n.x);
        b2 = float3(b, n.y * n.y*a - 1.0f, -n.y);
    } else {
        const float a = 1.0f / (1.0f + n.z);
        const float b = -n.x * n.y * a;
        b1 = float3(1.0f - n.x * n.x * a, b, -n.x);
        b2 = float3(b, 1.0f - n.y * n.y * a, -n.y);
    }
}

// Takes a depth value from a depth buffer and returns its world position.
float3 DecodePos(Texture2D<float> depth_buffer, uint2 pixel, float4x4 inverse_matrix, out bool is_back)
{
    uint2 resolution;
    depth_buffer.GetDimensions(resolution.x, resolution.y);
    float2 uv = ((float2)pixel + .5) / (float2)resolution;
    float depth = depth_buffer[pixel];
    is_back = depth == 0.0;
    float4 clip = float4(uv.x * 2. - 1., -uv.y * 2. + 1., depth, 1.);
    float4 world_pos = mul(inverse_matrix, clip);
    return world_pos.xyz / world_pos.w;
}

float2 SignNotZero(float2 xy)
{
	float2 result;
	result.x = xy.x >= 0 ? 1 : -1;
	result.y = xy.y >= 0 ? 1 : -1;
	return result;
}

float2 EncodeOctahedralMap(float3 normal) 
{
	// Project onto the octahedron.
	float3 octahedral = normal.xyz / (abs(normal.x) + abs(normal.y) + abs(normal.z));
	// Flatten onto square with coordinates in range [-1, 1].
	float2 result;
	if (octahedral.z >= 0.) {
		result = octahedral.xy;
	} else {
		result = SignNotZero(octahedral.xy) * (1. - abs(octahedral.yx));
	}
	return result;
}

float3 DecodeOctahedralMap(float2 encoded)
{
	float3 result;
	// Find point on octahedron.
	result.z = 1. - abs(encoded.x) - abs(encoded.y);
	if (result.z >= 0.) {
		result.xy = encoded;
	} else {
		result.xy = SignNotZero(encoded) * (1. - abs(encoded.yx));
	}
	// Project onto sphere.
	result = normalize(result);
	return result;
}