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

float4 EncodeTangentSpace(float3 normal, float4 tangent)
{
    float4 encoded;

    // Encode normal.
    encoded.xy = 0.5 * EncodeOctahedralMap(normal) + 0.5;

    // Encode tangent. 
    float3 canonical_tangent;
    float3 canonical_bitangent;
    CreateBasisAccurate(normal, canonical_tangent, canonical_bitangent);
    float angle = atan2(dot(tangent.xyz, canonical_bitangent), dot(tangent.xyz, canonical_tangent));
    encoded.z = (angle / TAU) + 0.5;

    // Encode winding.
    encoded.w = tangent.w == 1 ? 1 : 0;

    return encoded;
}

// TODO: This may be the wrong shift.
float4 UnpackTangentSpace(uint packed)
{
    float4 unpacked = float4(packed & 0x3ff, (packed >> 10) & 0x3ff, (packed >> 20) & 0x3ff, (packed >> 30) & 0x3);
    return unpacked /= float4(1023, 1023, 1023, 3);
}

uint PackTangentSpace(float4 unpacked)
{
    unpacked = clamp(unpacked, 0, 1);
    unpacked *= float4(1023, 1023, 1023, 3);
    unpacked += 0.5f;
    uint4 quantized = (uint4)unpacked;
    return quantized.x | (quantized.y << 10) | (quantized.z << 20) | (quantized.w << 30);
}