#include "Common.hlsli"

struct Parameters {
    int input_descriptor;
    int output_descriptor;
};

ConstantBuffer<Parameters> g_parameters: register(b0);
SamplerState g_sampler_equirectangular: register(s1);

[numthreads(8, 8, 1)]
void main(in uint3 thread_id: SV_DispatchThreadID)
{
    Texture2D<float4> input = ResourceDescriptorHeap[g_parameters.input_descriptor];
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[g_parameters.output_descriptor];

    uint3 output_dimensions;
    output.GetDimensions(output_dimensions.x, output_dimensions.y, output_dimensions.z);

    int face = thread_id.x / output_dimensions.x;
    int2 pixel = int2(thread_id.x % output_dimensions.x, thread_id.y);

    if (any(pixel >= output_dimensions.xy) || (face >= 6)) {
        return;
    }

    float3 u_direction;
    float3 v_direction;
    float3 face_direction;
    switch (face) {
        case 0: {
            face_direction = float3(1, 0, 0);
            u_direction = float3(0, 0, -1);
            v_direction = float3(0, -1, 0);
        } break;
        case 1: {
            face_direction = float3(-1, 0, 0);
            u_direction = float3(0, 0, 1);
            v_direction = float3(0, -1, 0);
        } break;
        case 2: {
            face_direction = float3(0, 1, 0);
            u_direction = float3(1, 0, 0);
            v_direction = float3(0, 0, 1);
        } break;
        case 3: {
            face_direction = float3(0, -1, 0);
            u_direction = float3(1, 0, 0);
            v_direction = float3(0, 0, -1);
        } break;
        case 4: {
            face_direction = float3(0, 0, 1);
            u_direction = float3(1, 0, 0);
            v_direction = float3(0, -1, 0);
        } break;
        case 5: {
            face_direction = float3(0, 0, -1);
            u_direction = float3(-1, 0, 0);
            v_direction = float3(0, -1, 0);
        } break;
        default: {
            return;
        }
    }
    float2 uv = PixelToUV(pixel, output_dimensions.xy);
    uv = uv * 2 - 1;
    float3 direction = normalize(face_direction + uv.x * u_direction + uv.y * v_direction);
    const float TAU = 6.28318530717;
    float2 equirectangular_coordinates = float2(atan2(direction.y, direction.x) / TAU, 1 - ((direction.z + 1) / 2));

    output[uint3(pixel, face)] = input.Sample(g_sampler_equirectangular, equirectangular_coordinates);
    output[uint3(pixel, face)].a = 1.0;
}