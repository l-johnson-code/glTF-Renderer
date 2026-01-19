struct Parameters {
    float4x4 clip_to_world;
};

struct VsIn {
    uint id: SV_VertexID;
};

struct VsOut {
    float4 pos: SV_POSITION;
    float3 direction: DIRECTION;
};

ConstantBuffer<Parameters> g_parameters;

void GenerateCameraRay(float2 clip, float4x4 clip_to_world, out float3 direction)
{
    float4 clip_start = float4(clip, 1, 1);
    float4 clip_end = float4(clip, 0, 1);
    float4 start = mul(clip_to_world, clip_start);
    float4 end = mul(clip_to_world, clip_end);
    float3 origin = start.xyz / start.w; 
    float3 destination = end.xyz / end.w;
    direction = destination - origin;
}

VsOut main(VsIn input) {
    VsOut output;
    
    float x = (float)(input.id & 1);
    float y = (float)((input.id >> 1) & 1);

    output.pos = float4(-1. + 4. * x, -1. + 4. * y, 0., 1.);

    float4 clip_start = float4(output.pos.xy, 1, 1);
    float4 clip_end = float4(output.pos.xy, 0, 1);
    float4 start = mul(g_parameters.clip_to_world, clip_start);
    float4 end = mul(g_parameters.clip_to_world, clip_end);
    start /= start.w;
    end /= end.w;
    output.direction = end.xyz - start.xyz;

    return output;
}