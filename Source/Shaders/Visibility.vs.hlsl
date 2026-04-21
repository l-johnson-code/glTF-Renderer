struct VSIn {
	float3 pos: POSITION;
};

struct VSOut {
	float4 pos: SV_POSITION;
};

struct PerFrame {
	float4x4 world_to_clip;
};

struct PerModel  {
	float4x4 model_to_world;
};

ConstantBuffer<PerFrame> per_frame: register(b0);
ConstantBuffer<PerModel> per_model: register(b1);

VSOut main(VSIn input)
{
	VSOut output;

	float4 world_pos = mul(per_model.model_to_world, float4(input.pos, 1.));
	output.pos = mul(per_frame.world_to_clip, world_pos);

	return output;
}