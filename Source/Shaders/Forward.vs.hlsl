#include "Vertex.hlsli"

struct VSIn {
	float3 pos: POSITION;
	float4 tangent_space: TANGENT_SPACE;
	float2 tex_coords[2]: TEXCOORD;
	float4 color: COLOR;
	float3 previous_pos: PREVIOUS_POS;
};

struct VSOut {
	float4 pos: SV_POSITION;
	float4 normal: NORMAL;
	float4 tangent: TANGENT;
	float2 tex_coords[2]: TEXCOORD;
	float4 color: COLOR;
	float4 previous_pos: POSITION;
	float3 world_pos: POS;
};

struct PerFrame {
	float4x4 world_to_clip;
	float4x4 previous_world_to_clip;
};

struct PerModel  {
	float4x4 model_to_world;
	float4x4 model_to_world_normals;
	float4x4 previous_model_to_world;
};

ConstantBuffer<PerFrame> per_frame: register(b0);
ConstantBuffer<PerModel> per_model: register(b1);

VSOut main(VSIn input)
{
	VSOut output;

	float4 world_pos = mul(per_model.model_to_world, float4(input.pos, 1.));
	output.pos = mul(per_frame.world_to_clip, world_pos);
	output.previous_pos = mul(per_frame.previous_world_to_clip, mul(per_model.previous_model_to_world, float4(input.previous_pos, 1.)));
	output.world_pos = world_pos.xyz;

	DecodeTangentSpace(input.tangent_space, output.normal.xyz, output.tangent);
	output.normal.xyz = mul(per_model.model_to_world_normals, float4(output.normal.xyz, 0)).xyz;
	output.tangent.xyz = mul(per_model.model_to_world, float4(output.tangent.xyz, 0)).xyz;

	output.tex_coords[0] = input.tex_coords[0];
	output.tex_coords[1] = input.tex_coords[1];

	output.color = input.color;

	return output;
}