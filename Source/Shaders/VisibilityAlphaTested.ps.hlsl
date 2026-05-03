#include "Material.hlsli"

struct PSIn {
	float4 pos: SV_POSITION;
    float2 tex_coords[2]: TEXCOORD;
    float4 color: COLOR;
	uint primitive_id: SV_PrimitiveID;
	bool front_face: SV_IsFrontFace;
};

struct PSOut {
	uint instance_id: SV_TARGET0;
	uint primitive_id: SV_TARGET1;
};

struct PerModel {
	uint instance_id;
    bool vertex_color;
    int material_id;
};

ConstantBuffer<PerModel> g_per_model: register(b0);
StructuredBuffer<Material> g_materials: register(t0);

PSOut main(PSIn input)
{
	PSOut output;
    
    // Discard pixel if alpha is zero.
    float4 vertex_color = g_per_model.vertex_color ? input.color : 1.0f;
    Material material = g_materials[g_per_model.material_id];
    float4 base_color = GetBaseColor(material, input.tex_coords, vertex_color);
    float alpha = GetAlpha(material, base_color);
    if (alpha == 0.0f) {
        discard;
    }
    
	output.instance_id = g_per_model.instance_id + 1;
	output.instance_id |= input.front_face ? 1u << 31 : 0;
	output.primitive_id = input.primitive_id + 1;
	return output;
}