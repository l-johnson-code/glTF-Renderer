#pragma once

enum MaterialFlags {
	MATERIAL_FLAG_NONE = 0,
	MATERIAL_FLAG_DOUBLE_SIDED = 1 << 0,
};

enum AlphaMode {
	ALPHA_MODE_OPAQUE,
	ALPHA_MODE_MASK,
	ALPHA_MODE_BLEND,
};

struct TextureAddress {
	int descriptor;
	int sampler_index;
	int tex_coord;
	float rotation;
	float2 offset;
	float2 scale;
};

struct Material {
	int flags;
	int alpha_mode;
	float metalness_factor;
	float roughness_factor;
	float4 base_color_factor;
	float occlusion_factor;
	float3 emissive_factor;
	float alpha_cutoff;
	float ior;
	float normal_scale;
	float pad_0;
	TextureAddress normal;
	TextureAddress albedo;
	TextureAddress metallic_roughness;
	TextureAddress occlusion;
	TextureAddress emissive;
	float specular_factor;
	float3 specular_color_factor;
	TextureAddress specular;
	TextureAddress specular_color;
	float clearcoat_factor;
	float clearcoat_roughness_factor;
	float clearcoat_normal_scale;
	float pad_1;
	TextureAddress clearcoat;
	TextureAddress clearcoat_roughness;
	TextureAddress clearcoat_normal;
	float anisotropy_strength;
	float anisotropy_rotation;
	float2 pad_2;
	TextureAddress anisotropy;
	float3 sheen_color_factor;
	float sheen_roughness_factor;
	TextureAddress sheen_color;
	TextureAddress sheen_roughness;
	float transmission_factor;
	float thickness_factor;
	float2 pad_3;
	TextureAddress transmission;
	float attenuation_distance;
	float3 attenuation_color;
	TextureAddress thickness;
};

float2 TransformUv(TextureAddress address, float2 uv)
{
	float3x3 translation = float3x3(
		1, 0, address.offset.x,
		0, 1, address.offset.y,
		0, 0, 1
	);
	float3x3 rotation = float3x3(
		cos(address.rotation), sin(address.rotation), 0,
		-sin(address.rotation), cos(address.rotation), 0,
		0, 0, 1
	);
	float3x3 scale = float3x3(
		address.scale.x, 0, 0,
		0, address.scale.y, 0,
		0, 0, 1
	);
	float3x3 transform = mul(translation, mul(rotation, scale));
	float2 uv_transformed = mul(transform, float3(uv, 1)).xy;
	return uv_transformed;
}

float4 SampleTexture(in Texture2D<float4> texture, in TextureAddress address, in float2 tex_coords[2])
{
	SamplerState texture_sampler = SamplerDescriptorHeap[address.sampler_index];
	float2 uv = tex_coords[address.tex_coord];
	uv = TransformUv(address, uv);
	return texture.SampleLevel(texture_sampler, uv, 0); // TODO: This doesn't support mip mapping, but is used because raytracing can't use the standard sample function!
}

float4 GetBaseColor(Material material, float2 texcoords[2], float4 vertex_color)
{
	float4 base_color = material.base_color_factor;
	base_color *= vertex_color;
	if (material.albedo.descriptor != -1) {
		base_color *= SampleTexture(ResourceDescriptorHeap[material.albedo.descriptor], material.albedo, texcoords);
	}
	return base_color;
}

float GetAlpha(Material material, float4 base_color)
{
    if (material.alpha_mode == ALPHA_MODE_BLEND) {
        return base_color.a;
    } else if (material.alpha_mode == ALPHA_MODE_MASK) {
        return base_color.a < material.alpha_cutoff ? 0 : 1;
    } else {
        return 1;
    }
}

float3 GetShadingNormal(Material material, float2 texcoords[2], float3 geometric_normal, float3x3 tangent_to_world)
{
    float3 shading_normal = geometric_normal;
	if (material.normal.descriptor != -1) {
		float3 normal_map = SampleTexture(ResourceDescriptorHeap[material.normal.descriptor], material.normal, texcoords).xyz * 2. - 1.;
		normal_map.xy *= material.normal_scale;
		shading_normal = normalize(mul(tangent_to_world, normal_map));
	}
    return shading_normal;
}

float2 GetMetalnessRoughness(Material material, float2 texcoords[2])
{
    float metalness = material.metalness_factor;
	float roughness = material.roughness_factor;
	if (material.metallic_roughness.descriptor != -1) {
		float4 metallic_roughness_sample = SampleTexture(ResourceDescriptorHeap[material.metallic_roughness.descriptor], material.metallic_roughness, texcoords);
		metalness *= metallic_roughness_sample.b;
		roughness *= metallic_roughness_sample.g;
	}
    return float2(metalness, roughness);
}

float GetOcclusion(Material material, float2 texcoords[2])
{
    float occlusion = 1.0;
	if (material.occlusion.descriptor != -1) {
		occlusion = SampleTexture(ResourceDescriptorHeap[material.occlusion.descriptor], material.occlusion, texcoords).r;
		occlusion = 1.0 + material.occlusion_factor * (occlusion - 1.0);
	}
    return occlusion;
}

float3 GetEmissive(Material material, float2 texcoords[2])
{
    // Emissive.
	float3 emissive = material.emissive_factor;
	if (material.emissive.descriptor != -1) {
		emissive *= SampleTexture(ResourceDescriptorHeap[material.emissive.descriptor], material.emissive, texcoords).rgb;
	}
    return emissive;
}

float GetSpecularFactor(Material material, float2 texcoords[2])
{
    float specular_factor = material.specular_factor;
	if (material.specular.descriptor != -1) {
		specular_factor *= SampleTexture(ResourceDescriptorHeap[material.specular.descriptor], material.specular, texcoords).a;
	}
    return specular_factor;
}

float3 GetSpecularColor(Material material, float2 texcoords[2])
{
	float3 specular_color = material.specular_color_factor;
	if (material.specular_color.descriptor != -1) {
	    specular_color *= SampleTexture(ResourceDescriptorHeap[material.specular_color.descriptor], material.specular_color, texcoords).rgb;
	}
    return specular_color;
}

float GetClearcoat(Material material, float2 texcoords[2])
{
    float clearcoat = material.clearcoat_factor;
	if (material.clearcoat.descriptor != -1) {
		clearcoat *= SampleTexture(ResourceDescriptorHeap[material.clearcoat.descriptor], material.clearcoat, texcoords).r;
	}
    return clearcoat;
}

float GetClearcoatRoughness(Material material, float2 texcoords[2])
{
	float clearcoat_roughness = material.clearcoat_roughness_factor;
	if (material.clearcoat_roughness.descriptor != -1) {
		clearcoat_roughness *= SampleTexture(ResourceDescriptorHeap[material.clearcoat_roughness.descriptor], material.clearcoat_roughness, texcoords).g;
	}
    return clearcoat_roughness;
}

float3 GetClearcoatNormal(Material material, float2 texcoords[2], float3 geometric_normal, float3x3 tangent_to_world)
{
	float3 clearcoat_normal = geometric_normal;
	if (material.clearcoat_normal.descriptor != -1) {
		float3 clearcoat_normal_map = SampleTexture(ResourceDescriptorHeap[material.clearcoat_normal.descriptor], material.clearcoat_normal, texcoords).xyz * 2. - 1.;
		clearcoat_normal_map.xy *= material.clearcoat_normal_scale;
		clearcoat_normal = normalize(mul(tangent_to_world, clearcoat_normal_map));
	}
    return clearcoat_normal;
}

float3 GetSheenColor(Material material, float2 texcoords[2])
{
	float3 sheen_color = material.sheen_color_factor;
	if (material.sheen_color.descriptor != -1) {
		sheen_color *= SampleTexture(ResourceDescriptorHeap[material.sheen_color.descriptor], material.sheen_color, texcoords).rgb;
	}
	return sheen_color;
}

float GetSheenRoughness(Material material, float2 texcoords[2])
{
	float sheen_roughness = material.sheen_roughness_factor;
	if (material.sheen_roughness.descriptor != -1) {
		sheen_roughness *= SampleTexture(ResourceDescriptorHeap[material.sheen_roughness.descriptor], material.sheen_roughness, texcoords).a;
	}
	return sheen_roughness;
}

float GetTransmission(Material material, float2 texcoords[2])
{
    float transmissive = material.transmission_factor;
	if (material.transmission.descriptor != -1) {
		transmissive *= SampleTexture(ResourceDescriptorHeap[material.transmission.descriptor], material.transmission, texcoords).r;
	}
    return transmissive;
}

float GetThickness(Material material, float2 texcoords[2])
{
    float thickness = material.thickness_factor;
	if (material.thickness.descriptor != -1) {
		thickness *= SampleTexture(ResourceDescriptorHeap[material.thickness.descriptor], material.thickness, texcoords).g;
	}
    return thickness;
}

float GetAnisotropyStrengthAndDirection(Material material, float2 texcoords[2], out float2 direction)
{
	// Anisotropy.
	float strength = material.anisotropy_strength;
	float rotation = material.anisotropy_rotation;
	float2x2 anisotropy_rotation_matrix = float2x2(
		cos(rotation), -sin(rotation),
		sin(rotation), cos(rotation)
	);
	float3 anisotropy_texture_value = float3(1, 0, 1);
	if (material.anisotropy.descriptor != -1) {
		anisotropy_texture_value = SampleTexture(ResourceDescriptorHeap[material.anisotropy.descriptor], material.anisotropy, texcoords).xyz;
		anisotropy_texture_value.xy = anisotropy_texture_value.xy * 2 - 1;
	}
	direction = normalize(mul(anisotropy_rotation_matrix, anisotropy_texture_value.xy));
	strength *= anisotropy_texture_value.z;
    return strength;
}

float3 CalculateShadingTangentAndBitangent(float3 shading_normal, float4 geometric_tangent, float3 geometric_bitangent, out float3 shading_bitangent)
{
    shading_bitangent = normalize(cross(shading_normal, geometric_tangent.xyz)); 
	float3 shading_tangent = normalize(cross(shading_bitangent, shading_normal));
	shading_bitangent *= geometric_tangent.w;
    return shading_tangent;
}

float3x3 TangentToWorldMatrix(float3 normal, float3 tangent, float3 bitangent)
{
    float3x3 tangent_to_world = transpose(float3x3(
		tangent,
		bitangent,
		normal
	));
    return tangent_to_world;
}