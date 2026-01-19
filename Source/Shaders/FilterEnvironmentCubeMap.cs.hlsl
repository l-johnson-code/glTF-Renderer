#include "Common.hlsli"
#include "Bsdf.hlsli"
#include "Random.hlsli"
#include "Sampling.hlsli"

enum Bsdf {
    BSDF_DIFFUSE,
    BSDF_GGX,
};

struct Parameters {
    int input_descriptor;
    int output_descriptor;
    float roughness;
    int num_of_samples;
    float mip_bias;
    int bsdf;
};

ConstantBuffer<Parameters> g_parameters: register(b0);
SamplerState g_sampler_linear_clamp: register(s0);

[numthreads(8, 8, 1)]
void main(in uint3 thread_id: SV_DispatchThreadID)
{
    TextureCube<float3> environment = ResourceDescriptorHeap[g_parameters.input_descriptor];
    RWTexture2DArray<float3> output = ResourceDescriptorHeap[g_parameters.output_descriptor];

    uint input_width = 0;
    uint input_height = 0;
    uint mip_count = 0;
    environment.GetDimensions(0, input_width, input_height, mip_count);

    uint3 output_dimensions = 0.xxx;
    output.GetDimensions(output_dimensions.x, output_dimensions.y, output_dimensions.z);

    uint3 output_pixel = float3(thread_id.x % output_dimensions.x, thread_id.y, thread_id.x / output_dimensions.x);
    if (!all(output_pixel < output_dimensions)) {
        return;
    }

    // Create a local basis.
    float3 normal = CubemapToDirection(output_pixel.z, PixelToUV(output_pixel.xy, output_dimensions.xy));
    float3 tangent;
    float3 bitangent;
    CreateBasis(normal, tangent, bitangent);
    float3x3 tangent_to_world = transpose(float3x3(
		tangent,
		bitangent,
		normal
	));

    float3 total = 0.xxx;
    float total_weight = 0;
    float a = g_parameters.roughness;
    
    // Importance sample cubemap with filtered importance sampling.
    for (int i = 0; i < g_parameters.num_of_samples; i++) {
        float3 n = normal;
        float3 v = n;

        float3 l;
        float pdf;
        float weight = 1;

        float2 u = R2(0.5.xx, i);
        
        if (g_parameters.bsdf == BSDF_GGX) {
            float3 h = SampleGgxNormal(a, u);
            pdf = GgxD(a, h.z) / 4.0;
            h = mul(tangent_to_world, h);
            l = reflect(-v, h);
            weight = saturate(dot(n, l));
        }
        if (g_parameters.bsdf == BSDF_DIFFUSE) {
            l = SampleCosineWeightedHemisphere(n, u);
            pdf = CosineWeightedHemispherePdf(n, l);
        }

        // Calculate the mip level to use for filtered importance sampling.
        float omega_s = 1.0 / ((float)g_parameters.num_of_samples * pdf);
        float omega_p = (4.0 * PI) / (6.0 * (float)input_width * (float)input_width);
        float mip_level = 0.5 * log2(omega_s / omega_p);
        mip_level = clamp(mip_level + g_parameters.mip_bias, 0, mip_count - 1);

        // Sample.
        total += weight * environment.SampleLevel(g_sampler_linear_clamp, l, mip_level);
        total_weight += weight;
    }
    output[output_pixel] = total / total_weight;
}