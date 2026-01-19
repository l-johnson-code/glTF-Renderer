struct Parameters {
    int input_descriptor;
    int output_descriptor;
    int2 input_resolution;
    int2 output_resolution;
};

ConstantBuffer<Parameters> g_parameters;

int Wrap(int index, int width)
{
    return index < width ? index : index - width;
}

float3 TrapezoidFilter(float3 s_0, float3 s_1, float3 s_2, float x, float n)
{
    float w_0 = (n - x) / (2 * n + 1);
    float w_1 = n / (2 * n + 1);
    float w_2 = (1 + x) / (2 * n + 1);
    return w_0 * s_0 + w_1 * s_1 + w_2 * s_2;
}

float3 VerticalPass(RWTexture2D<float3> input_texture, int kernel_size, int2 input_pixel, int output_y)
{
    switch (kernel_size) {
        case 3: {
            return TrapezoidFilter(
                input_texture[input_pixel],
                input_texture[input_pixel + int2(0, 1)],
                input_texture[float2(input_pixel.x, Wrap(input_pixel.y + 2, g_parameters.input_resolution.y))],
                output_y,
                g_parameters.output_resolution.y
            );
        } break;
        case 2: {
            return 0.5 * (input_texture[input_pixel] + input_texture[input_pixel + int2(0, 1)]);
        } break;
        case 1: {
            return input_texture[input_pixel + int2(0, 1)];
        } break;
        default: {
            return float3(0, 0, 0);
        }
    }
}

// TODO: Add clamping.
// TODO: Consider quick pass that uses the same algorithm for divisible by two and non divisible by two. This would be incorrect, introduce an image shift, and would break wrapping, but could work well for full screen passes that end up blurred anyway.
// Generates the next level in a mip chain using a box filter.
[numthreads(8, 8, 1)]
void main(in uint3 dispatch_thread_id: SV_DispatchThreadID)
{
    RWTexture2D<float3> input_texture = ResourceDescriptorHeap[g_parameters.input_descriptor];
    RWTexture2D<float3> output_texture = ResourceDescriptorHeap[g_parameters.output_descriptor];
    int2 kernel_size = select(g_parameters.input_resolution == 1, 1, select(g_parameters.input_resolution % 2, 3, 2));
    uint2 pixel = dispatch_thread_id.xy;
    float3 result;
    switch (kernel_size.x) {
        case 3: {
            float3 x_0 = VerticalPass(input_texture, kernel_size.y, 2 * pixel, pixel.y);
            float3 x_1 = VerticalPass(input_texture, kernel_size.y, 2 * pixel + int2(1, 0), pixel.y);
            float3 x_2 = VerticalPass(input_texture, kernel_size.y, int2(Wrap(2 * pixel.x + 2, g_parameters.input_resolution.x), 2 * pixel.y), pixel.y);
            result = TrapezoidFilter(x_0, x_1, x_2, pixel.x, g_parameters.output_resolution.x);
        } break;
        case 2: {
            float3 x_0 = VerticalPass(input_texture, kernel_size.y, 2 * pixel, pixel.y);
            float3 x_1 = VerticalPass(input_texture, kernel_size.y, 2 * pixel + int2(1, 0), pixel.y);
            result = 0.5 * (x_0 + x_1);
        } break;
        case 1: {
            result = VerticalPass(input_texture, kernel_size.y, 2 * pixel, pixel.y);
        } break;
    }
    output_texture[pixel] = result;
}