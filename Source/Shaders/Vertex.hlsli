#pragma once

#include "Common.hlsli"

void DecodeTangentSpace(float4 encoded, out float3 normal, out float4 tangent)
{
    // Decode normal.
    normal = DecodeOctahedralMap(encoded.xy * 2 - 1);
	
    // Decode tangent.
    float3 canonical_tangent;
    float3 canonical_bitangent;
    CreateBasisAccurate(normal, canonical_tangent, canonical_bitangent);
    float angle = TAU * encoded.z;
	tangent.xyz = cos(angle) * canonical_tangent + sin(angle) * canonical_bitangent;

    // Decode winding.
	tangent.w = encoded.w > 0 ? 1 : -1;
}

uint EncodeTangentSpace(float3 normal, float4 tangent)
{
    // Encode normal.
    float2 encoded_normal = 0.5 * EncodeOctahedralMap(normal) + 0.5;
    uint2 quantized_normal = clamp(encoded_normal, 0, 1) * 1023 + 0.5;

    // Decode normal to use in basis calculation.
	// This is to prevent numerical issues due to quantization.
	float2 unpacked_encoded_normal = quantized_normal / 1023.0f;
	normal = DecodeOctahedralMap(2.0f * unpacked_encoded_normal - 1.0f);

    // Encode tangent. 
    float3 canonical_tangent;
    float3 canonical_bitangent;
    CreateBasisAccurate(normal, canonical_tangent, canonical_bitangent);
    float angle = atan2(dot(tangent.xyz, canonical_bitangent), dot(tangent.xyz, canonical_tangent));
    float encoded_tangent = (angle / TAU) + 0.5;
    uint quantized_tangent = encoded_tangent * 1023 + 0.5;

    // Encode winding.
    uint quantized_winding = tangent.w == 1 ? 3 : 0;
    
    return quantized_normal.x | (quantized_normal.y << 10) | (quantized_tangent << 20) | (quantized_winding << 30);
}

float4 UnpackR10G10B10A2(uint packed)
{
    float4 unpacked = float4(packed & 0x3ff, (packed >> 10) & 0x3ff, (packed >> 20) & 0x3ff, (packed >> 30) & 0x3);
    return unpacked /= float4(1023, 1023, 1023, 3);
}