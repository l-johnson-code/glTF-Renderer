#include "Common.hlsli"
#include "Transforms.hlsli"
#include "Color.hlsli"

struct Parameters {
    int input_descriptor;
    int output_descriptor;
};

ConstantBuffer<Parameters> g_parameters: register(b0);

[numthreads(8, 8, 1)]
void main(in uint3 thread_id: SV_DispatchThreadID)
{
    RWTexture2D<float> input = ResourceDescriptorHeap[g_parameters.input_descriptor];
    RWTexture2D<float> output = ResourceDescriptorHeap[g_parameters.output_descriptor];
    
    uint2 output_resolution;
    output.GetDimensions(output_resolution.x, output_resolution.y);

    int2 pixel = thread_id.xy;

    if (any(pixel >= output_resolution)) {
        return;
    }

    float sum = input[2 * pixel];
    sum += input[2 * pixel + int2(0, 1)];
    sum += input[2 * pixel + int2(1, 0)];
    sum += input[2 * pixel + int2(1, 1)];
    output[pixel] = sum;
}