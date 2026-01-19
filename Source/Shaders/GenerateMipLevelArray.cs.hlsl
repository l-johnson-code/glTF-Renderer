struct Parameters {
    int input_descriptor;
    int output_descriptor;
};

ConstantBuffer<Parameters> g_parameters;

[numthreads(8, 8, 1)]
void main(in uint3 dispatch_thread_id: SV_DispatchThreadID)
{
    RWTexture2DArray<float3> input_texture_array = ResourceDescriptorHeap[g_parameters.input_descriptor];
    RWTexture2DArray<float3> output_texture_array = ResourceDescriptorHeap[g_parameters.output_descriptor];

    uint3 output_dimensions = 0.xxx;
    output_texture_array.GetDimensions(output_dimensions.x, output_dimensions.y, output_dimensions.z);

    uint3 output_pixel = uint3(dispatch_thread_id.x % output_dimensions.x, dispatch_thread_id.y, dispatch_thread_id.x / output_dimensions.x);
    if (!all(output_pixel < output_dimensions)) {
        return;
    }

    float3 result = 0;
    uint3 input_pixel = uint3(output_pixel.xy * 2, output_pixel.z);
    result += input_texture_array[input_pixel];
    result += input_texture_array[input_pixel + uint3(1, 0, 0)];
    result += input_texture_array[input_pixel + uint3(0, 1, 0)];
    result += input_texture_array[input_pixel + uint3(1, 1, 0)];
    result *= 0.25;

    output_texture_array[output_pixel] = result;
}