#include "Bsdf.hlsli"
#include "Lights.hlsli"
#include "Material.hlsli"

enum MeshFlags {
	MESH_FLAG_INDEX = 1 << 0,
	MESH_FLAG_NORMAL = 1 << 1,
	MESH_FLAG_TANGENT = 1 << 2,
	MESH_FLAG_TEXCOORD_0 = 1 << 3,
	MESH_FLAG_TEXCOORD_1 = 1 << 4,
	MESH_FLAG_COLOR = 1 << 5,
	MESH_FLAG_JOINT_WEIGHT = 1 << 6,
};

struct PSIn {
	float4 pos: SV_POSITION;
	float4 normal: NORMAL;
	float4 tangent: TANGENT;
	float2 tex_coords[2]: TEXCOORD;
	float4 color: COLOR;
	float4 previous_pos: POSITION;
	float3 world_pos: POS;
	bool is_front_face: SV_IsFrontFace;
};

struct PSOut {
	float4 lighting: SV_TARGET0;
	float2 motion_vector: SV_TARGET1;
};

enum RenderFlags {
	RENDER_FLAG_ENVIRONMENT = 1 << 0,
	RENDER_FLAG_POINT_LIGHTS = 1 << 1,
};

struct PerModel {
	uint32_t mesh_flags;
	int material_index;
	float4x4 model_to_world;
};

struct PerFrame {
	int2 viewport_xy;
	int num_of_lights;
	int ggx_cube_descriptor;
	float3 camera_pos;
	float environment_map_intensity;
	uint32_t render_flags;
	int diffuse_cube_descriptor;
	int transmission_descriptor;
};

ConstantBuffer<PerFrame> g_per_frame: register(b0);
ConstantBuffer<PerModel> g_per_model: register(b1);
StructuredBuffer<Light> g_lights: register(t0);
StructuredBuffer<Material> g_materials: register(t1);
SamplerState g_sampler_linear_clamp: register(s0);
SamplerState g_sampler_linear_wrap: register(s1);

float3 GenerateTangent(float3 normal)
{
    // Create a local coordinate system based on normal.
	float3 helper = float3(1, 0, 0);
	if (abs(normal.x) > abs(normal.y)) {
		helper = float3(0, 1, 0);
	}
    return normalize(cross(helper, normal));
}

float4 ClipToNormalizedDeviceCoordinates(float4 clip)
{
	return clip / clip.w;
}

float2 NormalizedDeviceCoordinatesToUV(float2 ndc)
{
	ndc.y = -ndc.y;
	return (ndc + 1.) / 2.;
}

float2 NormalizedDeviceCoordinatesToFramebufferCoordinates(float2 ndc, int2 resolution)
{
	return UnnormalizeTexelCoordinate(NormalizedDeviceCoordinatesToUV(ndc), resolution); 
}

float2 CalculateMotionVector(float4 pos, float4 previous_clip_pos, float2 resolution)
{
    float4 previous_ndc = ClipToNormalizedDeviceCoordinates(previous_clip_pos);
	float2 previous_frame_buffer_coords = NormalizedDeviceCoordinatesToFramebufferCoordinates(previous_ndc.xy, resolution);
	return previous_frame_buffer_coords - pos.xy;
}

float CalculateOcclusion(float occlusion_factor, float occlusion_texture_value)
{
    return 1.0 + occlusion_factor * (occlusion_texture_value - 1.0);
}

PSOut main(PSIn input)
{
	PSOut output;

	// Normals.
	float3 geometric_normal;
	if (g_per_model.mesh_flags & MESH_FLAG_NORMAL) {
		geometric_normal = normalize(input.normal.xyz);
	} else {
		float3 dpdx = ddx(input.world_pos.xyz);
		float3 dpdy = ddy(input.world_pos.xyz);
		geometric_normal = cross(dpdx, -dpdy);
		geometric_normal = normalize(geometric_normal);
	}

	// Reverse normal for back facing triangles.
	if (!input.is_front_face) {
		geometric_normal = -geometric_normal;
	}

	// Create tangent space to world space matrix.
	float4 geometric_tangent;
	float3 geometric_bitangent;
	if (g_per_model.mesh_flags & MESH_FLAG_TANGENT) {
		geometric_tangent = float4(normalize(input.tangent.xyz), input.tangent.w);
	} else {
		geometric_tangent = float4(GenerateTangent(geometric_normal), 1);
	}
	geometric_bitangent = input.tangent.w * normalize(cross(geometric_normal, geometric_tangent.xyz));
	float3x3 tangent_to_world =	TangentToWorldMatrix(geometric_normal, geometric_tangent.xyz, geometric_bitangent);

	SurfaceProperties surface_properties;
	Material material = g_materials[g_per_model.material_index];

	// Base color.
	float4 vertex_color = g_per_model.mesh_flags & MESH_FLAG_COLOR ? input.color : 1.xxxx;
	float4 base_color = GetBaseColor(material, input.tex_coords, vertex_color);
	surface_properties.albedo = base_color.rgb;

	// Alpha test.
	if (base_color.a < material.alpha_cutoff) {
		discard;
	}

	surface_properties.shading_normal = GetShadingNormal(material, input.tex_coords, geometric_normal, tangent_to_world);

	// Metallness and roughness.
    float2 metalness_rougness = GetMetalnessRoughness(material, input.tex_coords);
	surface_properties.metalness = metalness_rougness.x;
	surface_properties.roughness_squared.y = max(metalness_rougness.y * metalness_rougness.y, MINIMUM_ROUGHNESS);

	// Occlusion.
    float occlusion = GetOcclusion(material, input.tex_coords);

	// Emissive.
	float3 emissive = GetEmissive(material, input.tex_coords);

	// IOR.
	surface_properties.ior = material.ior;

	// Specular.
	surface_properties.specular_factor = GetSpecularFactor(material, input.tex_coords);
	surface_properties.specular_color = GetSpecularColor(material, input.tex_coords);

	// Clearcoat.
	surface_properties.clearcoat = GetClearcoat(material, input.tex_coords);
	surface_properties.clearcoat_roughness = GetClearcoatRoughness(material, input.tex_coords);
	surface_properties.clearcoat_normal = GetClearcoatNormal(material, input.tex_coords, geometric_normal, tangent_to_world);

	// Anisotropy.
	float2 anisotropy_direction;
	float anisotropy_strength = GetAnisotropyStrengthAndDirection(material, input.tex_coords, anisotropy_direction);

	// TODO: Is this the best way to calculate this? Will this work across different renderers? Is this how other renderers handle this (eg blender, babylon.js)?
	// TODO: Should the ideal matrix be a rotation in the axis cross(geometric_normal, normal)?
	float3 shading_bitangent;
	float3 shading_tangent = CalculateShadingTangentAndBitangent(surface_properties.shading_normal, geometric_tangent, geometric_bitangent, shading_bitangent);
	float3x3 shading_tangent_to_world = TangentToWorldMatrix(surface_properties.shading_normal, shading_tangent, shading_bitangent);

	surface_properties.anisotropy_tangent = normalize(mul(shading_tangent_to_world, float3(anisotropy_direction, 0)));
	surface_properties.anisotropy_bitangent = normalize(cross(surface_properties.anisotropy_tangent, surface_properties.shading_normal));
	surface_properties.roughness_squared.x = max(lerp(surface_properties.roughness_squared.y, 1, anisotropy_strength * anisotropy_strength), MINIMUM_ROUGHNESS);

	// Sheen.
	surface_properties.sheen_color = GetSheenColor(material, input.tex_coords);
	float sheen_roughness = GetSheenRoughness(material, input.tex_coords);
	surface_properties.sheen_roughness_squared = max(sheen_roughness * sheen_roughness, MINIMUM_ROUGHNESS);

	// Transmission.
	surface_properties.transmissive = GetTransmission(material, input.tex_coords);

	// Volume.
	surface_properties.thickness = GetThickness(material, input.tex_coords);
	float3 transmission_vector = normalize(input.world_pos - g_per_frame.camera_pos);
	transmission_vector = refract(transmission_vector, surface_properties.shading_normal, 1 / material.ior);
	float3 scale = float3(length(g_per_model.model_to_world[0].xyz), length(g_per_model.model_to_world[1].xyz), length(g_per_model.model_to_world[2].xyz));
	transmission_vector *= scale;
	surface_properties.thickness = length(transmission_vector);
	surface_properties.attenuation_distance = material.attenuation_distance;
	surface_properties.attenuation_color = material.attenuation_color;

	// Now calculate lighting.
	output.lighting.xyz = 0.xxx;
	// TODO: Emission should be effected by the clearcoat layer. See extension documentation for more info.
	output.lighting.xyz += emissive;

	float3 view = normalize(g_per_frame.camera_pos - input.world_pos);

	// Image based lighting.
	if ((g_per_frame.render_flags & RENDER_FLAG_ENVIRONMENT) && (g_per_frame.ggx_cube_descriptor != -1)) {
		TextureCube<float3> ggx_cube = ResourceDescriptorHeap[g_per_frame.ggx_cube_descriptor];
		uint ggx_width = 0;
		uint ggx_height = 0;
		uint ggx_mips = 0;
		ggx_cube.GetDimensions(0, ggx_width, ggx_height, ggx_mips);

		// Roughness to mip level.
		float mip = sqrt(surface_properties.roughness_squared.y) * (float)(ggx_mips - 1);
		mip = clamp(mip, 0, ggx_mips - 1);

		// Bent normal for anisotropy. 
		// Based on a technique from "Implementing Fur Using Deferred Shading" by Donald Revie in GPU Pro 2.
		float3 anisotropy_tangent = cross(surface_properties.anisotropy_bitangent, view);
		float3 anisotropy_normal = cross(anisotropy_tangent, surface_properties.anisotropy_bitangent);
		// Bend factor based on the glTF extension spec.
		float bend_factor = 1.0 - anisotropy_strength * (1.0 - sqrt(surface_properties.roughness_squared.y));
		bend_factor *= bend_factor;
		bend_factor *= bend_factor;
		float3 bent_normal = normalize(lerp(anisotropy_normal, surface_properties.shading_normal, bend_factor));

		float3 l = reflect(-view, bent_normal);
		float3 ld = ggx_cube.SampleLevel(g_sampler_linear_clamp, l, mip);
		ld *= g_per_frame.environment_map_intensity;
		
		float n_dot_v = saturate(dot(surface_properties.shading_normal, view));
		float a = surface_properties.roughness_squared.y;
		float a_2 = a * a;

		// Bias and scale approximation from "Approximate Models For Physically Based Rendering" by Angelo Pesce and Micha l Iwanicki.
		float bias = pow(2, -(7  * n_dot_v + 4 * a_2));
		float scale = 1 - bias - a_2 * max(bias, min(a, 0.739 + 0.323 * n_dot_v) - 0.434);
		
		float3 f0 = (1 - surface_properties.ior) / (1 + surface_properties.ior);
		f0 *= f0 * surface_properties.specular_color;
		f0 = min(f0, 1.xxx);
		float3 f90 = 1.xxx;
		float3 dfg = f0 * scale + f90 * bias;
		dfg *= surface_properties.specular_factor;
		float3 specular_ibl = dfg * ld;

		TextureCube<float3> diffuse_cube = ResourceDescriptorHeap[g_per_frame.diffuse_cube_descriptor];
		float3 diffuse_ibl = (1 - dfg) * surface_properties.albedo * g_per_frame.environment_map_intensity * diffuse_cube.SampleLevel(g_sampler_linear_clamp, surface_properties.shading_normal, 0);

		if (g_per_frame.transmission_descriptor != -1) {
			Texture2D<float3> transmission_texture = ResourceDescriptorHeap[g_per_frame.transmission_descriptor];
			uint transmission_width = 0;
			uint transmission_height = 0;
			uint transmission_mips = 0;
			transmission_texture.GetDimensions(0, transmission_width, transmission_height, transmission_mips);
			float transmission_a = ModulateRoughness(surface_properties.roughness_squared.y, surface_properties.ior);
			float transmission_mip = sqrt(transmission_a) * (float)(transmission_mips - 1);
			float2 transmission_uv = input.pos.xy / g_per_frame.viewport_xy;
			float3 transmission_ibl = surface_properties.albedo * transmission_texture.SampleLevel(g_sampler_linear_clamp, transmission_uv, transmission_mip);
			diffuse_ibl = lerp(diffuse_ibl, transmission_ibl, surface_properties.transmissive);
		}

		float3 dialectric_ibl = diffuse_ibl + specular_ibl;

		float3 metal_dfg = surface_properties.albedo * scale + f90 * bias;
		float3 metal_ibl = metal_dfg * ld;
		float3 ibl = lerp(dialectric_ibl, metal_ibl, surface_properties.metalness);

		if (surface_properties.clearcoat > 0) {
			float mip = surface_properties.clearcoat_roughness * (float)(ggx_mips - 1);
			mip = clamp(mip, 0, ggx_mips - 1);

			float3 l = reflect(-view, surface_properties.clearcoat_normal);
			float3 ld = ggx_cube.SampleLevel(g_sampler_linear_clamp, l, mip);
			ld *= g_per_frame.environment_map_intensity;

			ibl = FresnelCoat(1.5, surface_properties.clearcoat, ibl, ld, dot(surface_properties.clearcoat_normal, view));			
		}

		ibl *= occlusion;

		output.lighting.rgb += ibl;
	}

	// Point lighting.
	if (g_per_frame.render_flags & RENDER_FLAG_POINT_LIGHTS) {
		for (int i = 0; i < g_per_frame.num_of_lights; i++) {
			Light light = g_lights[i];
			LightRay light_ray = GetLightRay(light, input.world_pos);
			output.lighting.xyz += GltfBsdf(
				surface_properties,
				view,
				light_ray.direction,
				g_sampler_linear_clamp
			) * light_ray.color;
		}
	}

	output.motion_vector = CalculateMotionVector(input.pos, input.previous_pos, g_per_frame.viewport_xy);
	output.lighting.a = base_color.a;

	return output;
}