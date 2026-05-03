struct VSIn {
	float3 pos: POSITION;
    float2 tex_coords[2]: TEXCOORD;
    float4 color: COLOR;
};

struct VSOut {
	float4 pos: SV_POSITION;
    float2 tex_coords[2]: TEXCOORD;
    float4 color: COLOR;
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

    output.color = input.color;
    output.tex_coords[0] = input.tex_coords[0];
    output.tex_coords[1] = input.tex_coords[1];

	return output;
}