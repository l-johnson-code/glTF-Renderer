#pragma once
#include "Common.hlsli"

struct SurfaceProperties {
	float3 albedo;
	float alpha;
	float metalness;
	float2 roughness_squared;
	float3 shading_normal;
	float3 anisotropy_tangent;
	float3 anisotropy_bitangent;
	float ior;
	float3 specular_color;
	float specular_factor;
	float clearcoat;
	float clearcoat_roughness;
	float3 clearcoat_normal;
	float3 sheen_color;
	float sheen_roughness_squared;
	float transmissive;
	float thickness;
	float attenuation_distance;
	float3 attenuation_color;
};

static const float MINIMUM_ROUGHNESS = 0.001;

// Helper functions.
float Heavyside(float a)
{
    return a > 0 ? 1. : 0.;
}

float MaxValue(float3 color)
{
	return max(max(color.r, color.g), color.b);
}

float SchlickFresnel(float f0, float n_dot_v)
{
	return f0 + (1 - f0) * pow(1 - abs(n_dot_v), 5);
}

float3 SchlickFresnel(float3 f0, float n_dot_v)
{
	return f0 + (1 - f0) * pow(1 - abs(n_dot_v), 5);
}

// Specular BRDF.
float GgxD(float a, float n_dot_h)
{
	float a_2 = a * a;
    float numerator = a_2 * Heavyside(n_dot_h);
    float denominator = n_dot_h * n_dot_h * (a_2 - 1) + 1;
    denominator *= PI * denominator;
    return numerator / denominator;
}

float GgxSmithG1(float a, float n_dot_l, float h_dot_l)
{
    float a_2 = a * a;
    float numerator = 2 * n_dot_l * Heavyside(h_dot_l);
    float denominator = a_2 + (1 - a_2) * n_dot_l * n_dot_l;
    denominator = n_dot_l + sqrt(denominator);
    return numerator / denominator;
}

float GgxSeparableV(float a, float n_dot_l, float n_dot_v, float h_dot_l, float h_dot_v)
{
	float a_2 = a * a;
    float numerator = Heavyside(h_dot_l) * Heavyside(h_dot_v);
    float denominator = abs(n_dot_l) + sqrt(a_2 + (1 - a_2) * n_dot_l * n_dot_l);
    denominator *= abs(n_dot_v) + sqrt(a_2 + (1 - a_2) * n_dot_v * n_dot_v);
    return numerator / denominator;
}

float GgxCorrelatedV(float a, float n_dot_l, float n_dot_v, float h_dot_l, float h_dot_v)
{
	float a_2 = a * a;
    float numerator = 0.5 * Heavyside(h_dot_l) * Heavyside(h_dot_v);
    float denominator = abs(n_dot_v) * sqrt(a_2 + (1 - a_2) * n_dot_l * n_dot_l);
    denominator += abs(n_dot_l) * sqrt(a_2 + (1 - a_2) * n_dot_v * n_dot_v);
    return numerator / denominator;
}

float SpecularBrdf(float a, float n_dot_l, float n_dot_v, float n_dot_h, float h_dot_l, float h_dot_v)
{
    return GgxCorrelatedV(a, n_dot_l, n_dot_v, h_dot_l, h_dot_v) * GgxD(a, n_dot_h);
}

// Anisotropic specular BRDF.
float GgxAnisotropicD(float2 a, float3 h_local)
{
    float a_2 = a.x * a.y;
    float3 f = float3(a.yx * h_local.xy, a_2 * h_local.z);
    float w_2 = a_2 / dot(f, f);
    return a_2 * w_2 * w_2 / PI;
}

float GgxAnisotropicSmithG1(float2 a, float3 l_local, float l_dot_h)
{
	float numerator = 2 * l_local.z * Heavyside(l_dot_h);
    float denominator = l_local.z + length(float3(a * l_local.xy, l_local.z));
    return numerator / denominator;
}

// Not height correlated.
float GgxAnisotropicSeparableV(float2 a, float3 v_local, float3 l_local, float h_dot_v, float h_dot_l)
{
	float numerator = Heavyside(h_dot_v) * Heavyside(h_dot_l);
	float v = v_local.z + length(float3(a * v_local.xy, v_local.z));
	float l = l_local.z + length(float3(a * l_local.xy, l_local.z));
	return numerator / (v * l);
}

float GgxAnisotropicCorrelatedV(float2 a, float3 v_local, float3 l_local, float h_dot_v, float h_dot_l)
{
	float numerator = 0.5 * Heavyside(h_dot_v) * Heavyside(h_dot_l);
    float v = l_local.z * length(float3(a * v_local.xy, v_local.z));
    float l = v_local.z * length(float3(a * l_local.xy, l_local.z));
    return numerator / (v + l);
}

float AnisotropicSpecularBrdf(float2 a, float3 v_local, float3 h_local, float3 l_local)
{
	float h_dot_v = dot(h_local, v_local);
	float h_dot_l = dot(h_local, l_local);
    return GgxAnisotropicCorrelatedV(a, v_local, l_local, h_dot_v, h_dot_l) * GgxAnisotropicD(a, h_local);
}

float3 LambertDiffuse(float3 color)
{
    return color / PI;
}

float3 FresnelMix(float3 f0_color, float ior, float weight, float3 base, float3 layer, float h_dot_v)
{
    float3 f0 = (1 - ior) / (1 + ior);
	f0 *= f0 * f0_color;
	f0 = min(f0, 1.xxx);
    float3 fr = SchlickFresnel(f0, h_dot_v);
    return (1 - weight * MaxValue(fr)) * base + weight * fr * layer;
}

float3 ConductorFresnel(float3 specular, float3 f0, float h_dot_v)
{
    return specular * SchlickFresnel(f0, h_dot_v);
}

// Clearcoat.
float ClearcoatBrdf(float roughness_squared, float n_dot_l, float n_dot_v, float n_dot_h, float h_dot_l, float h_dot_v)
{
	return SpecularBrdf(roughness_squared, n_dot_l, n_dot_v, n_dot_h, h_dot_l, h_dot_v);
}

float3 FresnelCoat(float ior, float weight, float3 base, float3 layer, float n_dot_v)
{
	float f0 = (1 - ior) / (1 + ior);
	f0 *= f0;
	float fr = SchlickFresnel(f0, n_dot_v);
	return lerp(base, layer, weight * fr);
}

// Sheen.
float SheenNormalDistribution(float alpha, float n_dot_h)
{
	float inv_r = 1 / alpha;
	float cos2h = n_dot_h * n_dot_h;
	float sin2h = 1 - cos2h;
	float sheen_distribution = (2 + inv_r) * pow(sin2h, inv_r * 0.5) / (2 * PI);
	return sheen_distribution;
}

float SheenL(float alpha, float x)
{
	float t = (1 - alpha) * (1 - alpha);
	float a = lerp(21.5473, 25.3245, t);
	float b = lerp(3.82987, 3.32435, t);
	float c = lerp(0.19823, 0.16801, t);
	float d = lerp(-1.97760, -1.27393, t);
	float e = lerp(-4.32054, -4.85967, t);
	return a / (1 + b * pow(x, c)) + d * x + e;
}

float SheenShadowing(float alpha, float cos_theta)
{
	if (cos_theta < 0.5) {
		return exp(SheenL(alpha, cos_theta));
	} else {
		return exp(2 * SheenL(alpha, 0.5) - SheenL(alpha, 1 - cos_theta));
	}
}

float SheenVisibility(float alpha, float n_dot_l, float n_dot_v)
{
	return clamp(1 / ((1 + SheenShadowing(alpha, n_dot_l) + SheenShadowing(alpha, n_dot_v)) * 4 * n_dot_l * n_dot_v), 0, 1);
}

float SheenBrdf(float alpha, float n_dot_l, float n_dot_v, float n_dot_h)
{
	return SheenNormalDistribution(alpha, n_dot_h) * SheenVisibility(alpha, n_dot_v, n_dot_l);
}

float SheenE(float alpha, float cos_theta, SamplerState lookup_table_sampler)
{
	Texture2D<float> lookup_table = ResourceDescriptorHeap[STATIC_DESCRIPTOR_SRV_SHEEN_E];
	return lookup_table.SampleLevel(lookup_table_sampler, float2(cos_theta, alpha), 0);
}

float3 SheenMix(float3 material, float3 layer, float3 sheen_color, float alpha, float n_dot_l, float n_dot_v, SamplerState lookup_table_sampler)
{
	float sheen_albedo_scaling = min(1.0 - MaxValue(sheen_color) * SheenE(alpha, n_dot_v, lookup_table_sampler), 1.0 - MaxValue(sheen_color) * SheenE(alpha, n_dot_l, lookup_table_sampler));
	return sheen_color * layer + material * sheen_albedo_scaling;
}

// Volume.
// Attenuate light according to Beer's law.
float3 Attenuate(float attenuation_distance, float3 attenuation_color, float distance)
{
	if (attenuation_distance == 0) {
		return 1.xxx;
	} else {
		return pow(attenuation_color, distance / attenuation_distance);
	}
}

float3 GltfBsdf(SurfaceProperties surface_properties, float3 v, float3 l, SamplerState lookup_table_sampler) 
{
	const float outside_ior = 1.0; // The index of refraction of air.
	float2 a = surface_properties.roughness_squared;
    float3 n = surface_properties.shading_normal;
    float3 h = normalize(v + l);
    float3x3 world_to_tangent = float3x3( 
		surface_properties.anisotropy_tangent, 
		surface_properties.anisotropy_bitangent,
        surface_properties.shading_normal
	);
	float3 v_local = mul(world_to_tangent, v);
	float3 h_local = mul(world_to_tangent, h);
	float3 l_local = mul(world_to_tangent, l);
	float h_dot_l = dot(h, l);
	float h_dot_v = dot(h, v);

	// Base material.
    float3 specular = saturate(l_local.z) * AnisotropicSpecularBrdf(a, v_local, h_local, l_local).xxx;
	float3 diffuse = saturate(l_local.z) * LambertDiffuse(surface_properties.albedo);
    float3 dialectric = FresnelMix(surface_properties.specular_color, surface_properties.ior, surface_properties.specular_factor, diffuse, specular, h_dot_v);
    float3 metal = ConductorFresnel(specular, surface_properties.albedo, h_dot_v);
    float3 material = lerp(dialectric, metal, surface_properties.metalness);

	// Sheen.
	surface_properties.sheen_roughness_squared = clamp(surface_properties.sheen_roughness_squared, 0.000001, 1);
	float3 sheen_brdf = saturate(l_local.z) * SheenBrdf(surface_properties.sheen_roughness_squared, l_local.z, v_local.z, h_local.z);
	material = SheenMix(material, sheen_brdf, surface_properties.sheen_color, surface_properties.sheen_roughness_squared, l_local.z, v_local.z, lookup_table_sampler);

	// Clearcoat.
	float clearcoat_n_dot_v = dot(n, v);
	float clearcoat_n_dot_h = dot(n, h);
	float clearcoat_n_dot_l = dot(n, l);
	float clearcoat_brdf = saturate(clearcoat_n_dot_l) * ClearcoatBrdf(surface_properties.clearcoat_roughness, clearcoat_n_dot_l, clearcoat_n_dot_v, clearcoat_n_dot_h, h_dot_l, h_dot_v);
	float3 coated_material = FresnelCoat(1.5, surface_properties.clearcoat, material, clearcoat_brdf, clearcoat_n_dot_v);
    
	return coated_material;
}

