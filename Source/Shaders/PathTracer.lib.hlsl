#include "Lights.hlsli"
#include "Random.hlsli"
#include "Material.hlsli"
#include "Bsdf.hlsli"
#include "Transforms.hlsli"
#include "Color.hlsli"
#include "Sampling.hlsli"
#include "Vertex.hlsli"

enum DebugOutput {
    DEBUG_OUTPUT_NONE,
    DEBUG_OUTPUT_HIT_KIND,
    DEBUG_OUTPUT_VERTEX_COLOR,
    DEBUG_OUTPUT_VERTEX_ALPHA,
    DEBUG_OUTPUT_VERTEX_NORMAL,
    DEBUG_OUTPUT_VERTEX_TANGENT,
    DEBUG_OUTPUT_VERTEX_BITANGENT,
    DEBUG_OUTPUT_TEXCOORD_0,
    DEBUG_OUTPUT_TEXCOORD_1,
    DEBUG_OUTPUT_COLOR,
    DEBUG_OUTPUT_ALPHA,
    DEBUG_OUTPUT_SHADING_NORMAL,
    DEBUG_OUTPUT_SHADING_TANGENT,
    DEBUG_OUTPUT_SHADING_BITANGENT,
    DEBUG_OUTPUT_METALNESS,
    DEBUG_OUTPUT_ROUGHNESS,
    DEBUG_OUTPUT_SPECULAR,
    DEBUG_OUTPUT_SPECULAR_COLOR,
    DEBUG_OUTPUT_CLEARCOAT,
    DEBUG_OUTPUT_CLEARCOAT_ROUGHNESS,
    DEBUG_OUTPUT_CLEARCOAT_NORMAL,
    DEBUG_OUTPUT_TRANSMISSIVE,
    DEBUG_OUTPUT_BOUNCE_DIRECTION,
    DEBUG_OUTPUT_BOUNCE_BSDF,
    DEBUG_OUTPUT_BOUNCE_PDF,
    DEBUG_OUTPUT_BOUNCE_WEIGHT,
    DEBUG_BOUNCE_IS_TRANSMISSION,
    DEBUG_OUTPUT_HEMISPHERE_VIEW_SIDE,
    DEBUG_OUTPUT_ENVIRONMENT_MAP_DIRECTION,
    DEBUG_OUTPUT_ENVIRONMENT_MAP_COLOR,
    DEBUG_OUTPUT_ENVIRONMENT_MAP_PDF,
};

enum Flags {
	FLAG_NONE = 1 << 0,
	FLAG_CULL_BACKFACE = 1 << 1,
    FLAG_ACCUMULATE = 1 << 2,
    FLAG_LUMINANCE_CLAMP = 1 << 3,
	FLAG_INDIRECT_ENVIRONMENT_ONLY = 1 << 4,
    FLAG_POINT_LIGHTS = 1 << 5,
    FLAG_SHADOW_RAYS = 1 << 6,
    FLAG_ALPHA_SHADOWS = 1 << 7,
    FLAG_ENVIRONMENT_MAP = 1 << 8,
    FLAG_ENVIRONMENT_MIS = 1 << 9,
    FLAG_MATERIAL_DIFFUSE_WHITE = 1 << 10,
    FLAG_MATERIAL_USE_GEOMETRIC_NORMALS = 1 << 11,
    FLAG_MATERIAL_MIS = 1 << 12,
    FLAG_SHOW_NAN = 1 << 13,
    FLAG_SHOW_INF = 1 << 14,
    FLAG_SHADING_NORMAL_ADAPTATION = 1 << 15,
    FLAG_FILL_ALL_PIXELS = 1 << 16,
};

enum InstanceMask {
    MASK_NONE = 1 << 0,
    MASK_ALPHA_BLEND = 1 << 1,
};

enum HitGroupOffset {
    HIT_GROUP_OFFSET_SHADOW = 1,
};

enum MissShaderOffset {
    MISS_SHADER_OFFSET_SHADOW = 0,
};

enum GeometryPayloadFlags {
    GEOMETRY_PAYLOAD_FLAG_HIT = 1 << 0,
    GEOMETRY_PAYLOAD_FLAG_BACK_FACE = 1 << 1,
};

struct Instance {
	float4x4 transform;
	float4x4 normal_transform;
	int index_descriptor;
	int position_and_tangent_space_descriptor;
	int texcoord_descriptors[2];
	int color_descriptor;
	int material_id;
};

struct VertexAttributes {
    float3 position;
    float3 geometric_normal;
    float3 normal;
    float4 tangent;
    float3 bitangent;
    float4 color;
    float2 texcoords[2];
};

struct PositionAndTangentSpace {
    float3 position;
    uint tangent_space;
};

struct RayState {
    uint2 pixel;
    uint bounce;
    uint random_count;
    float3 origin;
    float3 direction;
    float3 throughput;
    float pdf;
    float3 color;
    bool use_mis;
    bool is_transmission;
    bool active;
};

struct GeometryPayload {
    uint flags;
    uint instance_index;
    uint primitive_index;
    float2 barycentrics;
};

struct ShadowPayload {
    float transmission;
};

struct SceneConstants {
    float4x4 clip_to_world;
    float4x4 world_to_clip;
    float3 camera_pos;
    int num_of_lights;
    uint2 resolution;
    uint32_t seed;
    int accumulated_frames;
    float3 environment_color;
    float environment_intensity;
    int debug_output;
    uint32_t flags;
    float max_ray_length;
    int min_bounces;
    int max_bounces;
    int output_descriptor;
    int environment_map_descriptor_id;
    int environment_alias_table;
    int environment_pdf;
    float luminance_clamp;
    float min_russian_roulette_continue_prob;
    float max_russian_roulette_continue_prob;
    float russian_roulette_active_lane_threshold;
    int v_buffer_primitive_id;
    int v_buffer_instance;
    int render_scale;
    int pixel_offset_x;
    int pixel_offset_y;
    int acceleration_structure_descriptor;
    int instances_descriptor;
    int materials_descriptor;
    int lights_descriptor;
};

ConstantBuffer<SceneConstants> g_scene_constants: register(b0);
SamplerState g_sampler_linear_clamp: register(s0);
SamplerState g_sampler_linear_wrap: register(s1);

float4 GenerateNextRandom(in out RayState ray_state)
{
    uint4 random = pcg4d(uint4(ray_state.pixel, g_scene_constants.seed, ray_state.random_count++));
    return random / 4294967295.0.xxxx;
}

float3 GenerateCameraRay(uint2 pixel, uint2 resolution, float4x4 clip_to_world, float3 camera_pos)
{
    float2 clip_space = (((float2)pixel + 0.5) / (float2)resolution) * 2 - 1;
    clip_space.y = -clip_space.y;
    float4 clip_end = float4(clip_space, 0, 1);
    float4 end = mul(clip_to_world, clip_end);
    float3 destination = end.xyz / end.w;
    return destination - camera_pos;
}

float3 CalculateBarycentrics(float2 tri_0, float2 tri_1, float2 tri_2, float2 pos)
{
    float2 v0 = tri_1 - tri_0;
    float2 v1 = tri_2 - tri_0;
    float2 v2 = pos - tri_0;
    float det = v0.x * v1.y - v1.x * v0.y;
    float3 barycentrics;
    barycentrics.y = v2.x * v1.y - v1.x * v2.y;
    barycentrics.z = v0.x * v2.y - v2.x * v0.y;
    barycentrics /= det;
    barycentrics.x = 1.0f - barycentrics.y - barycentrics.z;
    return barycentrics;
}

float3 BarycentricWeights(float2 barycentrics)
{
    return float3(1 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
}

template<typename T>
T BarycentricInterpolate(T a_0, T a_1, T a_2, float3 barycentric_weights)
{
    return barycentric_weights.x * a_0 + barycentric_weights.y * a_1 + barycentric_weights.z * a_2;
}

float3 CalculateFlatNormal(float3 p_0, float3 p_1, float3 p_2)
{
    return normalize(cross(p_1 - p_0, p_2 - p_0));
}

uint3 GetIndices(int index_descriptor, uint primitive_index)
{
    uint3 v = uint3(primitive_index * 3, primitive_index * 3 + 1, primitive_index * 3 + 2);
    if (index_descriptor != -1) {
        Buffer<uint> index_buffer = ResourceDescriptorHeap[NonUniformResourceIndex(index_descriptor)];
        v = uint3(index_buffer[v.x], index_buffer[v.y], index_buffer[v.z]);
    }
    return v;
}

float3 GetGeometricNormal(float3 pos_0, float3 pos_1, float3 pos_2)
{
    return cross(pos_1 - pos_0, pos_2 - pos_0);
}

void GetPositionAndTangentSpace(int position_and_tangent_space_descriptor, uint3 indices, float3 barycentric_weights, in out VertexAttributes attributes)
{
    StructuredBuffer<PositionAndTangentSpace> position_and_tangent_space = ResourceDescriptorHeap[NonUniformResourceIndex(position_and_tangent_space_descriptor)];
    PositionAndTangentSpace pos_tan_0 = position_and_tangent_space[indices.x];
    PositionAndTangentSpace pos_tan_1 = position_and_tangent_space[indices.y];
    PositionAndTangentSpace pos_tan_2 = position_and_tangent_space[indices.z];

    // Calculate position. 
    attributes.position = barycentric_weights.x * pos_tan_0.position + barycentric_weights.y * pos_tan_1.position + barycentric_weights.z * pos_tan_2.position;

    // Calculate normals and tangents.
    float3 normal_0;
    float4 tangent_0;
    DecodeTangentSpace(pos_tan_0.tangent_space, normal_0, tangent_0);
    float3 normal_1;
    float4 tangent_1;
    DecodeTangentSpace(pos_tan_1.tangent_space, normal_1, tangent_1);
    float3 normal_2;
    float4 tangent_2;
    DecodeTangentSpace(pos_tan_2.tangent_space, normal_2, tangent_2);
    attributes.normal = BarycentricInterpolate(normal_0, normal_1, normal_2, barycentric_weights);
    attributes.tangent.xyz = BarycentricInterpolate(tangent_0.xyz, tangent_1.xyz, tangent_2.xyz, barycentric_weights);
    attributes.tangent.w = tangent_0.w;

    attributes.geometric_normal = GetGeometricNormal(pos_tan_0.position, pos_tan_1.position, pos_tan_2.position);
}

float3 CalculateBitangent(float3 normal, float4 tangent)
{
    return tangent.w * normalize(cross(normal, tangent.xyz));
}

float4 GetVertexColor(int color_descriptor, uint3 vertex, float3 barycentric_weights)
{
    float4 color;
    if (color_descriptor != -1) {
        Buffer<float4> color_buffer = ResourceDescriptorHeap[NonUniformResourceIndex(color_descriptor)];
        float4 color_0 = color_buffer[vertex.x];
        float4 color_1 = color_buffer[vertex.y];
        float4 color_2 = color_buffer[vertex.z];
        color = BarycentricInterpolate(color_0, color_1, color_2, barycentric_weights);
    } else {
        color = 1.xxxx;
    }
    return color;
}

float2 GetTexcoord(int texcoord_descriptor, uint3 vertex, float3 barycentric_weights)
{
    float2 texcoord;
    if (texcoord_descriptor != -1) {
        StructuredBuffer<float2> texcoord_buffer = ResourceDescriptorHeap[NonUniformResourceIndex(texcoord_descriptor)];
        float2 texcoord_0 = texcoord_buffer[vertex.x];
        float2 texcoord_1 = texcoord_buffer[vertex.y];
        float2 texcoord_2 = texcoord_buffer[vertex.z];
        texcoord = BarycentricInterpolate(texcoord_0, texcoord_1, texcoord_2, barycentric_weights);
    } else {
        texcoord = 0.xx;
    }
    return texcoord;
}

// Adapted from Ray Tracing Gems Chapter 6.
float3 OffsetRay(float3 position, float3 geometric_normal)
{
    const float origin = 1.0f / 32.0f;
    const float float_scale = 1.0f / 65536.0f;
    const float int_scale = 256.0f;
    int3 of_i = int3(int_scale * geometric_normal);
    float3 p_i = asfloat(asint(position) + select(position < 0, -of_i, of_i));
    return select(abs(position) < origin, position + float_scale * geometric_normal, p_i);
}

void GetRemainingVertexAttributes(Instance instance, float3 indices, float3 barycentric_weights, in out VertexAttributes attributes)
{
    // Transform into world space.
    attributes.position = mul(instance.transform, float4(attributes.position, 1)).xyz;
    attributes.geometric_normal = normalize(mul(instance.normal_transform, float4(attributes.geometric_normal, 0)).xyz); // TODO: Should normals and tangents be normalized before they are transformed?
    attributes.normal = normalize(mul(instance.normal_transform, float4(attributes.normal, 0)).xyz); // TODO: Should normals and tangents be normalized before they are transformed?
    attributes.tangent.xyz = normalize(mul(instance.transform, float4(attributes.tangent.xyz, 0)).xyz);
    
    attributes.bitangent = CalculateBitangent(attributes.normal, attributes.tangent); // TODO: Should bitangents be calculated per vertex, and then interpolated with barycentric weights instead?
    attributes.color = GetVertexColor(instance.color_descriptor, indices, barycentric_weights);
    for (int i = 0; i < 2; i++) {
        attributes.texcoords[i] = GetTexcoord(instance.texcoord_descriptors[i], indices, barycentric_weights);
    }
}

void FlipNormals(in out VertexAttributes attributes)
{
    attributes.geometric_normal = -attributes.geometric_normal;
    attributes.normal = -attributes.normal;
    attributes.tangent = -attributes.tangent; // TODO: Is this necessary? Only significant if winding order is important.
}

VertexAttributes GetVertexAttributes(Instance instance, uint primitive_index, float3 barycentric_weights)
{
    VertexAttributes attributes;
    uint3 indices = GetIndices(instance.index_descriptor, primitive_index);
    GetPositionAndTangentSpace(instance.position_and_tangent_space_descriptor, indices, barycentric_weights, attributes);
    GetRemainingVertexAttributes(instance, indices, barycentric_weights, attributes);

    return attributes;
}

VertexAttributes GetVertexAttributesFromVisibilityBuffer(uint2 pixel, uint2 resolution, Instance instance, uint primitive_id, float4x4 world_to_clip)
{
    VertexAttributes attributes;

    uint3 indices = GetIndices(instance.index_descriptor, primitive_id);

    // Get positions and use them to calculate barycentrics.
    StructuredBuffer<PositionAndTangentSpace> position_and_tangent_space = ResourceDescriptorHeap[NonUniformResourceIndex(instance.position_and_tangent_space_descriptor)];
    PositionAndTangentSpace pos_tan_0 = position_and_tangent_space[indices.x];
    PositionAndTangentSpace pos_tan_1 = position_and_tangent_space[indices.y];
    PositionAndTangentSpace pos_tan_2 = position_and_tangent_space[indices.z];
    
    float4x4 model_to_clip = mul(world_to_clip, instance.transform);

    float4 clip_pos_0 = mul(model_to_clip, float4(pos_tan_0.position, 1));
    float4 clip_pos_1 = mul(model_to_clip, float4(pos_tan_1.position, 1));
    float4 clip_pos_2 = mul(model_to_clip, float4(pos_tan_2.position, 1));

    float2 clip_space = (((float2)pixel + 0.5) / (float2)resolution) * 2 - 1;
    clip_space.y = -clip_space.y;

    // Calculate barycentrics in normalized device coordinates.
    float3 barycentric_weights = CalculateBarycentrics(clip_pos_0.xy / clip_pos_0.w, clip_pos_1.xy / clip_pos_1.w, clip_pos_2.xy / clip_pos_2.w, clip_space);

    // Perspective correction.
    barycentric_weights /= float3(clip_pos_0.w, clip_pos_1.w, clip_pos_2.w);
    barycentric_weights /= barycentric_weights.x + barycentric_weights.y + barycentric_weights.z;

    // Calculate position. 
    attributes.position = barycentric_weights.x * pos_tan_0.position + barycentric_weights.y * pos_tan_1.position + barycentric_weights.z * pos_tan_2.position;

    // Calculate normals and tangents.
    float3 normal_0;
    float4 tangent_0;
    DecodeTangentSpace(pos_tan_0.tangent_space, normal_0, tangent_0);
    float3 normal_1;
    float4 tangent_1;
    DecodeTangentSpace(pos_tan_1.tangent_space, normal_1, tangent_1);
    float3 normal_2;
    float4 tangent_2;
    DecodeTangentSpace(pos_tan_2.tangent_space, normal_2, tangent_2);
    attributes.normal = BarycentricInterpolate(normal_0, normal_1, normal_2, barycentric_weights);
    attributes.tangent.xyz = BarycentricInterpolate(tangent_0.xyz, tangent_1.xyz, tangent_2.xyz, barycentric_weights);
    attributes.tangent.w = tangent_0.w;

    attributes.geometric_normal = GetGeometricNormal(pos_tan_0.position, pos_tan_1.position, pos_tan_2.position);

    // Get other vertex data.
    GetRemainingVertexAttributes(instance, indices, barycentric_weights, attributes);
    
    return attributes;
}

// This prevents black patches on mirror like surfaces.
// Based on "Local Shading Normal Adaption" section in "The Iray Light Transport Simulation and Rendering System".
float3 NormalAdaptation(float3 ng, float3 ns, float3 v)
{
    // Normal adaptation.
    float3 r = reflect(-v, ns);
    float r_dot_ng = dot(r, ng);
    if (r_dot_ng < 0) {
        return normalize(v + normalize(r - r_dot_ng * ng));
    } else {
        return ns;
    }
}

SurfaceProperties GetSurfaceProperties(Material material, VertexAttributes attributes, float3 view)
{
	float3x3 tangent_to_world =	TangentToWorldMatrix(attributes.normal, attributes.tangent.xyz, attributes.bitangent);

	SurfaceProperties surface_properties;

	float4 base_color = GetBaseColor(material, attributes.texcoords, attributes.color);
	surface_properties.albedo = base_color.rgb;

    surface_properties.alpha = GetAlpha(material, base_color);

	surface_properties.shading_normal = GetShadingNormal(material, attributes.texcoords, attributes.normal, tangent_to_world);

    if (g_scene_constants.flags & FLAG_SHADING_NORMAL_ADAPTATION) {
        surface_properties.shading_normal = NormalAdaptation(attributes.geometric_normal, surface_properties.shading_normal, view);
    }

    float2 metalness_rougness = GetMetalnessRoughness(material, attributes.texcoords);
	surface_properties.metalness = metalness_rougness.x;
	surface_properties.roughness_squared.y = max(metalness_rougness.y * metalness_rougness.y, MINIMUM_ROUGHNESS);

    float occlusion = GetOcclusion(material, attributes.texcoords);

	float3 emissive = GetEmissive(material, attributes.texcoords);

	surface_properties.ior = material.ior;

	surface_properties.specular_factor = GetSpecularFactor(material, attributes.texcoords);
	surface_properties.specular_color = GetSpecularColor(material, attributes.texcoords);

	surface_properties.clearcoat = GetClearcoat(material, attributes.texcoords);
	surface_properties.clearcoat_roughness = GetClearcoatRoughness(material, attributes.texcoords);
	surface_properties.clearcoat_normal = GetClearcoatNormal(material, attributes.texcoords, attributes.normal, tangent_to_world);

    if (g_scene_constants.flags & FLAG_SHADING_NORMAL_ADAPTATION) {
        surface_properties.clearcoat_normal = NormalAdaptation(attributes.geometric_normal, surface_properties.clearcoat_normal, view);
    }

	float2 anisotropy_direction;
	float anisotropy_strength = GetAnisotropyStrengthAndDirection(material, attributes.texcoords, anisotropy_direction);

	// TODO: Is this the best way to calculate this? Will this work across different renderers? Is this how other renderers handle this (eg blender, babylon.js)?
	// TODO: Should the ideal matrix be a rotation in the axis cross(geometric_normal, normal)?
	float3 shading_bitangent;
	float3 shading_tangent = CalculateShadingTangentAndBitangent(surface_properties.shading_normal, attributes.tangent, attributes.bitangent, shading_bitangent);
	float3x3 shading_tangent_to_world = TangentToWorldMatrix(surface_properties.shading_normal, shading_tangent, shading_bitangent);

	surface_properties.anisotropy_tangent = normalize(mul(shading_tangent_to_world, float3(anisotropy_direction, 0)));
	surface_properties.anisotropy_bitangent = normalize(cross(surface_properties.anisotropy_tangent, surface_properties.shading_normal));
	surface_properties.roughness_squared.x = max(lerp(surface_properties.roughness_squared.y, 1, anisotropy_strength * anisotropy_strength), MINIMUM_ROUGHNESS);

	surface_properties.sheen_color = GetSheenColor(material, attributes.texcoords);
	float sheen_roughness = GetSheenRoughness(material, attributes.texcoords);
	surface_properties.sheen_roughness_squared = max(sheen_roughness * sheen_roughness, MINIMUM_ROUGHNESS);

	surface_properties.transmissive = GetTransmission(material, attributes.texcoords);

    // TODO: Volume required different technique for raytracing.
    surface_properties.thickness = GetThickness(material, attributes.texcoords);
    surface_properties.attenuation_distance = material.attenuation_distance;
	surface_properties.attenuation_color = material.attenuation_color;

    // Use a minimum roughness value so that we don't have to have handle delta bounces.
    surface_properties.roughness_squared = max(surface_properties.roughness_squared, MINIMUM_ROUGHNESS.xx);
    surface_properties.clearcoat_roughness = max(surface_properties.clearcoat_roughness, MINIMUM_ROUGHNESS);

    // Use the geometric normal if this flag is set.
    if (g_scene_constants.flags & FLAG_MATERIAL_USE_GEOMETRIC_NORMALS) {
        surface_properties.shading_normal = attributes.geometric_normal;
        surface_properties.clearcoat_normal = attributes.geometric_normal;
    }

    return surface_properties;
}

float BalanceHeuristic(float pdf, float other_pdf)
{
    return pdf / (pdf + other_pdf);
}

float3 SampleAlpha(SurfaceProperties surface_properties, float3 v, out float3 alpha)
{
    alpha = 1.xxx;
    return -v;
}

float3 SampleClearcoat(SurfaceProperties surface_properties, float3 v, float2 u)
{
    float3 n = surface_properties.clearcoat_normal;
    float3 t, b;
    CreateBasis(n, t, b);
    float3x3 world_to_local = float3x3(t, b, n);
    float3x3 local_to_world = transpose(world_to_local);
    float3 v_local = mul(world_to_local, v);
    float3 h_local = SampleGgxNormal(surface_properties.clearcoat_roughness, u);
    float3 h = mul(local_to_world, h_local);
    float3 l = reflect(-v, h);
    return l;
}

float ClearcoatPdf(SurfaceProperties surface_properties, float3 v, float3 l)
{
    float a = surface_properties.clearcoat_roughness;
    float3 n = surface_properties.clearcoat_normal;
    float3 h = normalize(v + l);
    float pdf = GgxNormalPdf(a, n, h);
    pdf /= 4 * dot(v, h); // Reflection jacobian.
    return pdf;
}

float3 SampleSheen(SurfaceProperties surface_properties, float3 v, float2 u)
{
    return SampleCosineWeightedHemisphere(surface_properties.shading_normal, u);
}

float SheenPdf(SurfaceProperties surface_properties, float3 v, float3 l)
{
    return CosineWeightedHemispherePdf(surface_properties.shading_normal, l);
}

float3 SampleSpecular(SurfaceProperties surface_properties, float3 v, float2 u)
{
    // Create a basis and transforms.
    float3 n = surface_properties.shading_normal;
    float3 t = surface_properties.anisotropy_tangent;
    float3 b = surface_properties.anisotropy_bitangent;
    float3x3 world_to_local = float3x3(t, b, n);
    float3x3 local_to_world = transpose(world_to_local);
    float3 v_local = mul(world_to_local, v);
    float3 h_local = SampleGgxAnisotropicNormal(surface_properties.roughness_squared, u);
    float3 h = mul(local_to_world, h_local);
    float3 l = reflect(-v, h);

    return l;
}

float SpecularPdf(SurfaceProperties surface_properties, float3 v, float3 l)
{
    float2 a = surface_properties.roughness_squared;
    float3 t = surface_properties.anisotropy_tangent;
    float3 b = surface_properties.anisotropy_bitangent;
    float3 n = surface_properties.shading_normal;
    float3 h = normalize(v + l);

    float3x3 world_to_local = float3x3(t, b, n);
    float3 local_v = mul(world_to_local, v);
    float3 local_h = mul(world_to_local, h);

    float pdf = GgxAnisotropicNormalPdf(a, local_h);
    pdf /= 4 * dot(v, h); // Reflection jacobian.
    
    return pdf;
}

float3 SampleDiffuse(SurfaceProperties surface_properties, float3 v, float2 u)
{
    return SampleCosineWeightedHemisphere(surface_properties.shading_normal, u);
}

float DiffusePdf(SurfaceProperties surface_properties, float3 l)
{
    return CosineWeightedHemispherePdf(surface_properties.shading_normal, l);
}

float3 SampleTransmission(SurfaceProperties surface_properties, float3 v, float2 u)
{
    // Create a basis and transforms.
    float3 n = surface_properties.shading_normal;
    float3 t = surface_properties.anisotropy_tangent;
    float3 b = surface_properties.anisotropy_bitangent;
    float3x3 world_to_local = float3x3(t, b, n);
    float3x3 local_to_world = transpose(world_to_local);
    float a = ModulateRoughness(surface_properties.roughness_squared.y, surface_properties.ior);
    float3 h_local = SampleGgxNormal(a, u);
    float3 h = mul(local_to_world, h_local);
    float3 l = reflect(-v, h);
    l = l - 2 * dot(n, l) * n;

    return l;
}

float TransmissionPdf(SurfaceProperties surface_properties, float3 v, float3 l)
{
    float a = ModulateRoughness(surface_properties.roughness_squared.y, surface_properties.ior);
    float3 n = surface_properties.shading_normal;
    l = l - 2 * dot(n, l) * n;
    float3 h = normalize(v + l);

    float pdf = GgxNormalPdf(a, n, h);
    pdf /= 4 * dot(v, h); // Reflection jacobian.
    
    return pdf;
}

enum BsdfLayer {
    BSDF_LAYER_DIFFUSE,
    BSDF_LAYER_SPECULAR,
    BSDF_LAYER_SHEEN,
    BSDF_LAYER_CLEARCOAT,
    BSDF_LAYER_ALPHA,
    BSDF_LAYER_TRANSMISSION,
};

BsdfLayer SelectBsdf(SurfaceProperties surface_properties, float3 v, float u, float alpha_prob, float clearcoat_prob, float sheen_prob, float specular_prob, float transmission_prob)
{
    if (u <= alpha_prob) {
        return BSDF_LAYER_ALPHA;
    }
    u -= alpha_prob;
    if (u <= clearcoat_prob) {
        return BSDF_LAYER_CLEARCOAT;
    }
    u -= clearcoat_prob;
    if (u <= sheen_prob) {
        return BSDF_LAYER_SHEEN;
    }
    u -= sheen_prob;
    if (u <= specular_prob) {
        return BSDF_LAYER_SPECULAR;
    }
    u -= specular_prob;
    if (u <= transmission_prob) {
        return BSDF_LAYER_TRANSMISSION;
    }
    return BSDF_LAYER_DIFFUSE;
}

void LayerProbabilities(SurfaceProperties surface_properties, float3 v, out float alpha_prob, out float clearcoat_prob, out float sheen_prob, out float specular_prob, out float diffuse_prob, out float transmission_prob)
{
    float remaining_prob = 1;
    alpha_prob = 1.0 - surface_properties.alpha;
    remaining_prob -= alpha_prob;
    clearcoat_prob = FresnelCoat(1.5, surface_properties.clearcoat, 0.xxx, 1.xxx, dot(surface_properties.clearcoat_normal, v)).x;
    clearcoat_prob *= remaining_prob;
    remaining_prob -= clearcoat_prob;
    sheen_prob = any(surface_properties.sheen_color > 0) ? 0.5 : 0.0;
    sheen_prob *= remaining_prob;
    remaining_prob -= sheen_prob;
    specular_prob = 0.5;
    specular_prob *= remaining_prob;
    remaining_prob -= specular_prob;
    transmission_prob = surface_properties.transmissive;
    transmission_prob *= remaining_prob;
    remaining_prob -= transmission_prob;
    diffuse_prob = remaining_prob;
}

float BsdfPdf(SurfaceProperties surface_properties, float3 v, float3 l, bool is_transmission, float clearcoat_prob, float sheen_prob, float specular_prob, float diffuse_prob, float transmission_prob)
{
    if (is_transmission) {
        return transmission_prob * TransmissionPdf(surface_properties, v, l);
    }
    float pdf = clearcoat_prob * ClearcoatPdf(surface_properties, v, l);
    pdf += sheen_prob * SheenPdf(surface_properties, v, l);
    pdf += specular_prob * SpecularPdf(surface_properties, v, l);
    pdf += diffuse_prob * DiffusePdf(surface_properties, l);
    return pdf;
}

float3 EvaluateBsdf(SurfaceProperties surface_properties, float3 geometric_normal, float3 v, float3 l, out float pdf)
{
    if (g_scene_constants.flags & FLAG_MATERIAL_DIFFUSE_WHITE) {
        float n_dot_l = saturate(dot(surface_properties.shading_normal, l));
        pdf = n_dot_l / PI;
        return n_dot_l / PI;
    }

    if (g_scene_constants.flags & FLAG_MATERIAL_MIS) {
        float diffuse_prob = 0;
        float specular_prob = 0;
        float sheen_prob = 0;
        float clearcoat_prob = 0;
        float alpha_prob = 0;
        float transmission_prob = 0;
        bool is_transmission = (dot(geometric_normal, l) * dot(geometric_normal, v)) < 0;

        LayerProbabilities(surface_properties, v, alpha_prob, clearcoat_prob, sheen_prob, specular_prob, diffuse_prob, transmission_prob);
        pdf = BsdfPdf(surface_properties, v, l, is_transmission, clearcoat_prob, sheen_prob, specular_prob, diffuse_prob, transmission_prob);
        return surface_properties.alpha * GltfBsdf(surface_properties, v, l, is_transmission, g_sampler_linear_clamp);
    }

    float n_dot_l = saturate(dot(surface_properties.shading_normal, l));
    pdf = n_dot_l / PI;
    pdf *= surface_properties.alpha;
    return surface_properties.alpha * GltfBsdf(surface_properties, v, l, g_sampler_linear_clamp);
}

float3 SampleBsdf(SurfaceProperties surface_properties, float3 u, float3 v, out float3 l, out float pdf, out bool is_transmission, out bool use_mis)
{
    if (g_scene_constants.flags & FLAG_MATERIAL_DIFFUSE_WHITE) {
        use_mis = true;
        is_transmission = false;
        float3 n = surface_properties.shading_normal;
        l = SampleCosineWeightedHemisphere(n, u.yz);
        pdf = CosineWeightedHemispherePdf(n, l);
        return dot(n, l) / PI;
    }

    // Sample using multiple importance sampling. 
    if (g_scene_constants.flags & FLAG_MATERIAL_MIS) {

        float diffuse_prob = 0;
        float specular_prob = 0;
        float sheen_prob = 0;
        float clearcoat_prob = 0;
        float alpha_prob = 0;
        float transmission_prob = 0;

        // Select a BSDF to sample.
        is_transmission = false;
        use_mis = true;
        LayerProbabilities(surface_properties, v, alpha_prob, clearcoat_prob, sheen_prob, specular_prob, diffuse_prob, transmission_prob);
        BsdfLayer layer = SelectBsdf(surface_properties, v, u.x, alpha_prob, clearcoat_prob, sheen_prob, specular_prob, transmission_prob);
        switch (layer) {
            case BSDF_LAYER_ALPHA: {
                l = -v;
                use_mis = false;
                pdf = alpha_prob;
                is_transmission = true;
                return (1-surface_properties.alpha).xxx;
            } break;
            case BSDF_LAYER_DIFFUSE: {
                l = SampleDiffuse(surface_properties, v, u.yz);
            } break;
            case BSDF_LAYER_SPECULAR: {
                l = SampleSpecular(surface_properties, v, u.yz);
            } break;
            case BSDF_LAYER_SHEEN: {
                l = SampleSheen(surface_properties, v, u.yz);
            } break;
            case BSDF_LAYER_CLEARCOAT: {
                l = SampleClearcoat(surface_properties, v, u.yz);
            } break;
            case BSDF_LAYER_TRANSMISSION: {
                l = SampleTransmission(surface_properties, v, u.yz);
                is_transmission = true;
            } break;
        }
        pdf = BsdfPdf(surface_properties, v, l, is_transmission, clearcoat_prob, sheen_prob, specular_prob, diffuse_prob, transmission_prob);
        return surface_properties.alpha * GltfBsdf(surface_properties, v, l, is_transmission, g_sampler_linear_clamp);
    }

    // Use cosine weighted hemisphere sampling.
    if (u.x > surface_properties.alpha) {
        l = -v;
        use_mis = false;
        pdf = (1-surface_properties.alpha);
        is_transmission = true;
        return (1-surface_properties.alpha).xxx;
    } else {
        use_mis = true;
        is_transmission = false;
        float3 n = surface_properties.shading_normal;
        l = SampleCosineWeightedHemisphere(n, u.yz);
        pdf = CosineWeightedHemispherePdf(n, l);
        pdf *= surface_properties.alpha;
        return surface_properties.alpha * GltfBsdf(surface_properties, v, l, g_sampler_linear_clamp);
    }

}

LightRay SamplePointLight(float3 surface_pos, float u, out float pdf)
{
    uint light_index = clamp(((uint)(u * (float)g_scene_constants.num_of_lights)), 0, g_scene_constants.num_of_lights - 1);
    StructuredBuffer<Light> lights = ResourceDescriptorHeap[g_scene_constants.lights_descriptor];
    Light light = lights[light_index];
    pdf = 1.0f / (float)g_scene_constants.num_of_lights;
    return GetLightRay(light, surface_pos);
}

LightRay SampleEnvironmentMap(float3 u, out float pdf)
{
    StructuredBuffer<AliasMap> alias_table = ResourceDescriptorHeap[g_scene_constants.environment_alias_table];
    TextureCube<float4> environment_map = ResourceDescriptorHeap[g_scene_constants.environment_map_descriptor_id];

    // Importance sample the environment map texture.
    vector<uint16_t, 2> pixel = SampleAliasMap(alias_table, u.x, pdf);
    float2 uv = ((float2)pixel + u.yz) / float2(1024, 1024);
    
    float3 direction = SquareToSphere(UvToUnitSquare(uv));

    // Create the light ray.
    LightRay light_ray;
    light_ray.direction = direction;
    light_ray.color = g_scene_constants.environment_intensity * environment_map.SampleLevel(g_sampler_linear_wrap, direction, 0).rgb;
    
    return light_ray;
}

float EnvironmentMapPdf(float3 l)
{
    Texture2D<float> pdf_texture = ResourceDescriptorHeap[g_scene_constants.environment_pdf];
    float2 uv = UnitSquareToUv(SphereToSquare(l));
    uint2 pixel = UVToPixel(uv, int2(1024, 1024)); // TODO: Remove hardcoded size.
    float pdf = pdf_texture[pixel];
    return pdf;
}

bool RussianRouletteTerminate(float min_continue_prob, float max_continue_prob, float u, in out float3 throughput) 
{
    float continue_prob = clamp(Luminance(throughput), min_continue_prob, max_continue_prob);
    if (u < continue_prob) {
        throughput /= continue_prob;
        return false;
    } else {
        return true;
    }
}

// A version of russian roulette that terminates entire warps based on the number of active lanes in a wave.
// Based on the implementation in the "Path Tracing Nanite in NVIDIA Zorah" presentation.
bool WaveBasedRussianRouletteTerminate(float min_continue_prob, float max_continue_prob, float active_lane_ratio_threshold, float u, in out float3 throughput)
{
    float continue_prob = clamp(Luminance(throughput), min_continue_prob, max_continue_prob);
    uint active_lanes = WaveActiveCountBits(true);
    uint total_lanes = WaveGetLaneCount();
    float active_lane_ratio = (float)active_lanes / (float)total_lanes;
    float group_continue_prob = continue_prob * saturate(active_lane_ratio / active_lane_ratio_threshold);
    if (u < group_continue_prob) {
        throughput /= group_continue_prob;
        return false;
    } else {
        return true;
    }
}

float TraceShadowRay(RaytracingAccelerationStructure acceleration_structure, float3 origin, float3 direction, bool alpha_shadow)
{
    if (g_scene_constants.flags & FLAG_INDIRECT_ENVIRONMENT_ONLY) {
        return 1.0;
    }
    float light_transmission = 1.0;
    uint ray_flags = g_scene_constants.flags & FLAG_CULL_BACKFACE ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES : 0;
    ray_flags |= RAY_FLAG_SKIP_CLOSEST_HIT_SHADER;
    RayDesc shadow_ray = {origin, 0, direction, g_scene_constants.max_ray_length};
    ShadowPayload shadow_payload = {0.0};
    if (alpha_shadow) {
        shadow_payload.transmission = 1.0;
        ray_flags |= RAY_FLAG_FORCE_NON_OPAQUE;
    } else {
        ray_flags |= RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH;
    }
    TraceRay(acceleration_structure, ray_flags, 0xff, HIT_GROUP_OFFSET_SHADOW, 0, MISS_SHADER_OFFSET_SHADOW, shadow_ray, shadow_payload);
    return shadow_payload.transmission;
}

bool EnvironmentMapNextEventEstimation(VertexAttributes vertex_attributes, SurfaceProperties surface_properties, RaytracingAccelerationStructure acceleration_structure, in out RayState ray_state)
{
    // Importance sample environment map.
    if ((g_scene_constants.flags & FLAG_ENVIRONMENT_MAP) && (g_scene_constants.flags & FLAG_ENVIRONMENT_MIS)) {
        float light_pdf;
        LightRay light_ray = SampleEnvironmentMap(GenerateNextRandom(ray_state).xyz, light_pdf);
        if (g_scene_constants.debug_output == DEBUG_OUTPUT_ENVIRONMENT_MAP_DIRECTION) {
            ray_state.color = (light_ray.direction + 1) / 2;
            return ray_state.active = false;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_ENVIRONMENT_MAP_COLOR) {
            ray_state.color = light_ray.color;
            return ray_state.active = false;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_ENVIRONMENT_MAP_PDF) {
            ray_state.color = light_pdf;
            return ray_state.active = false;
        }
        float3 origin = OffsetRay(vertex_attributes.position, vertex_attributes.geometric_normal);
        light_ray.color *= TraceShadowRay(acceleration_structure, origin, light_ray.direction, false);
        if (any(light_ray.color > 0.0)) {
            float bsdf_pdf = 0;
            float3 bsdf = EvaluateBsdf(surface_properties, vertex_attributes.geometric_normal, -ray_state.direction, light_ray.direction, bsdf_pdf);
            float mis = BalanceHeuristic(light_pdf, bsdf_pdf);
            ray_state.color += (ray_state.throughput * mis * bsdf * light_ray.color) / light_pdf;
        }
    }
    return ray_state.active;
}

void NextEventEstimation(VertexAttributes vertex_attributes, SurfaceProperties surface_properties, RaytracingAccelerationStructure acceleration_structure, in out RayState ray_state)
{
    // Sample point lights using next event estimation.
    if ((g_scene_constants.flags & FLAG_POINT_LIGHTS) && (g_scene_constants.num_of_lights > 0)) {
        float pdf;
        LightRay light_ray = SamplePointLight(vertex_attributes.position, GenerateNextRandom(ray_state).x, pdf);
        if (g_scene_constants.flags & FLAG_SHADOW_RAYS) {
            float3 origin = OffsetRay(vertex_attributes.position, vertex_attributes.geometric_normal);
            light_ray.color *= TraceShadowRay(acceleration_structure, origin, light_ray.direction, g_scene_constants.flags & FLAG_ALPHA_SHADOWS);
        }
        if (any(light_ray.color > 0.0)) {
            float bsdf_pdf = 0;
            float3 bsdf = EvaluateBsdf(surface_properties, vertex_attributes.geometric_normal, -ray_state.direction, light_ray.direction, bsdf_pdf);
            ray_state.color += (ray_state.throughput * bsdf * light_ray.color) / pdf;
        }
    }
}

bool GenerateBounceRay(VertexAttributes vertex_attributes, SurfaceProperties surface_properties, in out RayState ray_state)
{
    // Generate the next bounce ray.
    float3 v = -ray_state.direction;
    float4 u = GenerateNextRandom(ray_state);
    float3 bsdf = SampleBsdf(surface_properties, u.xyz, v, ray_state.direction, ray_state.pdf, ray_state.is_transmission, ray_state.use_mis);
    float3 weight = ray_state.pdf != 0 ? bsdf / ray_state.pdf : 0;
    ray_state.throughput *= weight;

    // Bounce ray debug outputs.
    if (g_scene_constants.debug_output == DEBUG_OUTPUT_BOUNCE_DIRECTION) {
        ray_state.color = 0.5 * (ray_state.direction + 1);
        return ray_state.active = false;
    } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_BOUNCE_BSDF) {
        ray_state.color = bsdf;
        return ray_state.active = false;
    } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_BOUNCE_PDF) {
        ray_state.color = ray_state.pdf;
        return ray_state.active = false;
    } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_BOUNCE_WEIGHT) {
        ray_state.color = weight;
        return ray_state.active = false;
    } else if (g_scene_constants.debug_output == DEBUG_BOUNCE_IS_TRANSMISSION) {
        ray_state.color = ray_state.is_transmission ? float3(0, 1, 0) : float3(1, 0, 0);
        return ray_state.active = false;
    }

    ray_state.origin = vertex_attributes.position;
    ray_state.bounce++;
    return ray_state.active;
}

bool ContinueRay(in out RayState ray_state)
{
    // Terminate bounces if throughput reaches 0.
    if (all(ray_state.throughput <= 0.0f)) {
        ray_state.active = false;
    }

    // Terminate bounces using Russian roulette.
    float4 u = GenerateNextRandom(ray_state);
    if ((ray_state.bounce >= g_scene_constants.min_bounces) && WaveBasedRussianRouletteTerminate(g_scene_constants.min_russian_roulette_continue_prob, g_scene_constants.max_russian_roulette_continue_prob, g_scene_constants.russian_roulette_active_lane_threshold, u.x, ray_state.throughput)) {
        ray_state.active = false;
    }

    return ray_state.active;
}

GeometryPayload TraceBounceRay(VertexAttributes vertex_attributes, RaytracingAccelerationStructure acceleration_structure, uint ray_flags, in out RayState ray_state)
{
    ray_state.origin = OffsetRay(ray_state.origin, ray_state.is_transmission ? -vertex_attributes.geometric_normal : vertex_attributes.geometric_normal);
    
    // Force bounces to miss all geometry and hit the environment if this flag is set.
    uint instance_mask = g_scene_constants.flags & FLAG_INDIRECT_ENVIRONMENT_ONLY ? 0 : 0xff;

    RayDesc ray;
    ray.Origin = ray_state.origin;
    ray.Direction = ray_state.direction;
    ray.TMin = 0.0f;
    ray.TMax = g_scene_constants.max_ray_length;
    
    GeometryPayload payload;
    payload.flags = 0;
    payload.barycentrics = 0.0f.xx;
    payload.instance_index = 0;
    payload.primitive_index = 0;

    TraceRay(acceleration_structure, ray_flags, instance_mask, 0, 0, 0, ray, payload);

    return payload;
}

void EvaluateEnvironment(in out RayState ray_state)
{
    if (g_scene_constants.flags & FLAG_ENVIRONMENT_MAP) {
        TextureCube<float4> environment_map = ResourceDescriptorHeap[g_scene_constants.environment_map_descriptor_id];
        float mis_weight = 1.0;
        if ((g_scene_constants.flags & FLAG_ENVIRONMENT_MIS) && ray_state.use_mis) {
            float environment_map_pdf = EnvironmentMapPdf(ray_state.direction);
            mis_weight = BalanceHeuristic(ray_state.pdf, environment_map_pdf);
        }
        ray_state.color += ray_state.throughput * mis_weight * g_scene_constants.environment_intensity * environment_map.SampleLevel(g_sampler_linear_wrap, ray_state.direction, 0).rgb;
    } else {
        ray_state.color += ray_state.throughput * g_scene_constants.environment_intensity * g_scene_constants.environment_color;
    }
}

[shader("raygeneration")]
void RayGeneration()
{
    RayState ray_state;
    ray_state.active = true;
    ray_state.bounce = 0;
    ray_state.color = 0.xxx;
    ray_state.direction = 0.xxx;
    ray_state.origin = 0.xxx;
    ray_state.pdf = 1.0f;
    ray_state.pixel = DispatchRaysIndex().xy * g_scene_constants.render_scale + int2(g_scene_constants.pixel_offset_x, g_scene_constants.pixel_offset_y);
    ray_state.random_count = 0;
    ray_state.throughput = 1.0f.xxx;
    ray_state.use_mis = false;
    ray_state.is_transmission = false;

    uint ray_flags = g_scene_constants.flags & FLAG_CULL_BACKFACE ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES : 0;
    bool back_facing = false;
    uint instance_mask = 0xff;
    float4 u = 0.xxxx;
    VertexAttributes vertex_attributes;
    StructuredBuffer<Instance> instances = ResourceDescriptorHeap[g_scene_constants.instances_descriptor];
    Instance instance;

    // Get data from visibility buffer.
    Texture2D<uint> v_buffer_primitive_id = ResourceDescriptorHeap[g_scene_constants.v_buffer_primitive_id];
    Texture2D<uint> v_buffer_instance = ResourceDescriptorHeap[g_scene_constants.v_buffer_instance];
    uint2 visibility_buffer_pixel = DispatchRaysIndex().xy;
    uint2 visibility_buffer_resolution = (g_scene_constants.resolution + g_scene_constants.render_scale - 1) / g_scene_constants.render_scale;
    uint primitive_id = v_buffer_primitive_id[visibility_buffer_pixel];
    uint instance_index = v_buffer_instance[visibility_buffer_pixel] & 0x7fffffffu;
    back_facing = !(bool)(v_buffer_instance[visibility_buffer_pixel] & 0x80000000u);
    if (primitive_id == 0 || instance_index == 0) {
        // If no geometry was hit, sample the environment.
        if (g_scene_constants.flags & FLAG_ENVIRONMENT_MAP) {
            TextureCube<float4> environment_map = ResourceDescriptorHeap[g_scene_constants.environment_map_descriptor_id];
            float3 direction = GenerateCameraRay(visibility_buffer_pixel, visibility_buffer_resolution, g_scene_constants.clip_to_world, g_scene_constants.camera_pos);
            ray_state.color = g_scene_constants.environment_intensity * environment_map.SampleLevel(g_sampler_linear_wrap, direction, 0).rgb;
        } else {
            ray_state.color = g_scene_constants.environment_intensity * g_scene_constants.environment_color;
        }
        ray_state.active = false;
    } else {
        primitive_id--;
        instance_index--;
        instance = instances[instance_index];
        vertex_attributes = GetVertexAttributesFromVisibilityBuffer(visibility_buffer_pixel, visibility_buffer_resolution, instance, primitive_id, g_scene_constants.world_to_clip);
        if (back_facing) {
            FlipNormals(vertex_attributes);
        }
        ray_state.origin = g_scene_constants.camera_pos;
        ray_state.direction = normalize(vertex_attributes.position - g_scene_constants.camera_pos);
    }

    RaytracingAccelerationStructure acceleration_structure = ResourceDescriptorHeap[g_scene_constants.acceleration_structure_descriptor];

    while (ray_state.active) {

        // Vertex attribute debug outputs.
        if (g_scene_constants.debug_output == DEBUG_OUTPUT_HIT_KIND) {
            ray_state.color = back_facing ? float3(1, 0, 0) : float3(0, 1, 0);
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_VERTEX_COLOR) {
            ray_state.color = vertex_attributes.color.rgb;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_VERTEX_ALPHA) {
            ray_state.color = vertex_attributes.color.aaa;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_VERTEX_NORMAL) {
            ray_state.color = (vertex_attributes.normal + 1) / 2;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_VERTEX_TANGENT) {
            ray_state.color = (vertex_attributes.tangent.xyz + 1) / 2;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_VERTEX_BITANGENT) {
            ray_state.color = (vertex_attributes.bitangent + 1) / 2;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_TEXCOORD_0) {
            ray_state.color = float3(vertex_attributes.texcoords[0], 0);
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_TEXCOORD_1) {
            ray_state.color = float3(vertex_attributes.texcoords[1], 0);
            break;
        }

        // Get surface properties for shading.
        StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_scene_constants.materials_descriptor];
        Material material = materials[instance.material_id];
        SurfaceProperties surface_properties = GetSurfaceProperties(material, vertex_attributes, -ray_state.direction);

        // Material debug output.
        if (g_scene_constants.debug_output == DEBUG_OUTPUT_COLOR) {
            ray_state.color = surface_properties.albedo;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_ALPHA) {
            ray_state.color = surface_properties.alpha.xxx;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_SHADING_NORMAL) {
            ray_state.color = (surface_properties.shading_normal + 1) / 2;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_SHADING_TANGENT) {
            ray_state.color = (surface_properties.anisotropy_tangent + 1) / 2;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_SHADING_BITANGENT) {
            ray_state.color = (surface_properties.anisotropy_bitangent + 1) / 2;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_METALNESS) {
            ray_state.color = surface_properties.metalness;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_ROUGHNESS) {
            ray_state.color = sqrt(surface_properties.roughness_squared.y);
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_SPECULAR) {
            ray_state.color = surface_properties.specular_factor.xxx;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_SPECULAR_COLOR) {
            ray_state.color = surface_properties.specular_color;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_CLEARCOAT) {
            ray_state.color = surface_properties.clearcoat.xxx;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_CLEARCOAT_ROUGHNESS) {
            ray_state.color = surface_properties.clearcoat_roughness.xxx;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_CLEARCOAT_NORMAL) {
            ray_state.color = (surface_properties.clearcoat_normal + 1) / 2;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_TRANSMISSIVE) {
            ray_state.color = surface_properties.transmissive.xxx;
            break;
        } else if (g_scene_constants.debug_output == DEBUG_OUTPUT_HEMISPHERE_VIEW_SIDE) {
            ray_state.color = dot(-ray_state.direction, surface_properties.shading_normal) > 0 ? float3(0, 1, 0) : float3(1, 0, 0);
            break;
        }

        // Emissive.
        float3 emissive = GetEmissive(material, vertex_attributes.texcoords);
        ray_state.color += ray_state.throughput * emissive;

        // Sample the environment map.
        if (!EnvironmentMapNextEventEstimation(vertex_attributes, surface_properties, acceleration_structure, ray_state)) {
            break;
        }
        
        // Sample point lights.
        NextEventEstimation(vertex_attributes, surface_properties, acceleration_structure, ray_state);

        // Break loop if max bounces exeeded.
        if (ray_state.bounce >= g_scene_constants.max_bounces) {
            break;
        }

        if (!GenerateBounceRay(vertex_attributes, surface_properties, ray_state)) {
            break;
        }

        // Terminate bounces using Russian roulette.
        if (!ContinueRay(ray_state)) {
            break;
        }

        // Trace bounce ray and get intersected geometry.
        GeometryPayload geometry_payload = TraceBounceRay(vertex_attributes, acceleration_structure, ray_flags, ray_state);

        // Sample the environment if we haven't hit any geometry.
        if (!(geometry_payload.flags & GEOMETRY_PAYLOAD_FLAG_HIT)) {
            EvaluateEnvironment(ray_state);
            ray_state.active = false;
            break;
        }

        // Get interpolated vertex attributes.
        instance = instances[geometry_payload.instance_index];
        float3 barycentric_weights = BarycentricWeights(geometry_payload.barycentrics);
        vertex_attributes = GetVertexAttributes(instance, geometry_payload.primitive_index, barycentric_weights);

        // Flip normals if we hit the back side of the triangle.
        back_facing = geometry_payload.flags & GEOMETRY_PAYLOAD_FLAG_BACK_FACE;
        if (back_facing) {
            FlipNormals(vertex_attributes);
        }
    }

    // Display any floating point errors.
    if (any(isnan(ray_state.color))) {
        ray_state.color = g_scene_constants.flags & FLAG_SHOW_NAN ? float3(1, 0, 0) : 0.xxx;
    }
    if (any(isinf(ray_state.color))) {
        ray_state.color = g_scene_constants.flags & FLAG_SHOW_INF ? float3(1, 0, 0) : 0.xxx;
    }

    // Luminance clampling to help with fireflies. 
    if (g_scene_constants.flags & FLAG_LUMINANCE_CLAMP) {
        float luminance = Luminance(ray_state.color);
        if (luminance > g_scene_constants.luminance_clamp) {
            ray_state.color *= g_scene_constants.luminance_clamp / luminance; 
        }
    }

    // Accumulate result.
    RWTexture2D<float4> output = ResourceDescriptorHeap[g_scene_constants.output_descriptor];
    if ((g_scene_constants.flags & FLAG_ACCUMULATE) && (g_scene_constants.accumulated_frames != 0)) {
        float4 history = output[ray_state.pixel];
        float blend_factor = 1.0 / ((float)g_scene_constants.accumulated_frames + 1.0);
        float4 accumulated = lerp(history, float4(ray_state.color, 1.0), blend_factor);
        output[ray_state.pixel] = accumulated;
    } else {
        if (g_scene_constants.flags & FLAG_FILL_ALL_PIXELS) {
            for (int i = 0; i < g_scene_constants.render_scale; i++) {
                for (int j = 0; j < g_scene_constants.render_scale; j++) {
                    output[DispatchRaysIndex().xy * g_scene_constants.render_scale + int2(i, j)] = float4(ray_state.color, 1.0);
                }
            }
        } else {
            output[ray_state.pixel] = float4(ray_state.color, 1.0);
        }
    }
}

[shader("closesthit")]
void ClosestHit(inout GeometryPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    payload.flags |= GEOMETRY_PAYLOAD_FLAG_HIT;
    if (HitKind() == HIT_KIND_TRIANGLE_BACK_FACE) {
        payload.flags |= GEOMETRY_PAYLOAD_FLAG_BACK_FACE;
    }
    payload.instance_index = InstanceIndex();
    payload.primitive_index = PrimitiveIndex();
    payload.barycentrics = attributes.barycentrics;
}

// Any hit shader for alpha masked materials.
[shader("anyhit")]
void AnyHit(inout GeometryPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    // Gather surface properties.
    uint instance_index = InstanceIndex();
    uint primitive_index = PrimitiveIndex();
    float3 barycentric_weights = BarycentricWeights(attributes.barycentrics);
   
    StructuredBuffer<Instance> instances = ResourceDescriptorHeap[g_scene_constants.instances_descriptor];
    StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_scene_constants.materials_descriptor];
    Instance instance = instances[instance_index];
    Material material = materials[instance.material_id];

    // Get interpolated vertex attributes.
    uint3 vertices = GetIndices(instance.index_descriptor, primitive_index);
    float4 base_color = GetVertexColor(instance.color_descriptor, vertices, barycentric_weights);
    float2 texcoords[2];
    for (int i = 0; i < 2; i++) {
        texcoords[i] = GetTexcoord(instance.texcoord_descriptors[i], vertices, barycentric_weights);
    }
    base_color = GetBaseColor(material, texcoords, base_color);

    if (base_color.a < material.alpha_cutoff) {
		IgnoreHit();
	}
}

[shader("anyhit")]
void ShadowAnyHit(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    // Gather surface properties.
    uint instance_index = InstanceIndex();
    uint primitive_index = PrimitiveIndex();
    float3 barycentric_weights = BarycentricWeights(attributes.barycentrics);
   
    StructuredBuffer<Instance> instances = ResourceDescriptorHeap[g_scene_constants.instances_descriptor];
    StructuredBuffer<Material> materials = ResourceDescriptorHeap[g_scene_constants.materials_descriptor];
    Instance instance = instances[instance_index];
    Material material = materials[instance.material_id];

    // Get interpolated vertex attributes.
    uint3 vertices = GetIndices(instance.index_descriptor, primitive_index);
    float4 base_color = GetVertexColor(instance.color_descriptor, vertices, barycentric_weights);
    float2 texcoords[2];
    for (int i = 0; i < 2; i++) {
        texcoords[i] = GetTexcoord(instance.texcoord_descriptors[i], vertices, barycentric_weights);
    }
    base_color = GetBaseColor(material, texcoords, base_color);
    float alpha = GetAlpha(material, base_color);
    payload.transmission *= 1 - alpha;
    if (payload.transmission == 0.0) {
        AcceptHitAndEndSearch();
    }
}

[shader("miss")]
void ShadowMiss(inout ShadowPayload payload)
{
    payload.transmission = 1.0f;
}