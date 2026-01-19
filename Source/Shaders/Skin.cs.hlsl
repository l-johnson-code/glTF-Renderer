#include "Common.hlsli"

enum MeshFlags {
    MESH_FLAG_INDEX = 1 << 0,
    MESH_FLAG_NORMAL = 1 << 1,
    MESH_FLAG_TANGENT = 1 << 2,
    MESH_FLAG_TEXCOORD_0 = 1 << 3,
    MESH_FLAG_TEXCOORD_1 = 1 << 4,
    MESH_FLAG_COLOR = 1 << 5,
    MESH_FLAG_JOINT_WEIGHT = 1 << 6,
};

enum DynamicMeshFlags {
    DYNAMIC_MESH_FLAG_POSITION = 1 << 0,
    DYNAMIC_MESH_FLAG_NORMAL = 1 << 1,
    DYNAMIC_MESH_FLAG_TANGENT = 1 << 2,
};

enum MorphFlags {
    MORPH_FLAG_POSITIONS = 1 << 0,
    MORPH_FLAG_NORMALS = 1 << 1,
    MORPH_FLAG_TANGENTS = 1 << 2,
};

struct BoneWeights {
    uint32_t bones[4];
    float weights[4];
};

struct PerModel {
    uint32_t num_of_vertices;
    uint32_t input_mesh_flags;
    uint32_t output_mesh_flags;
    int num_of_morph_targets;
    struct {
        float weight;
        int position_descriptor;
        int normal_descriptor;
        int tangent_descriptor;
    } morph_targets[4];
};

struct Bone {
    float4x4 transform;
    float4x4 inverse_transpose;
};

ConstantBuffer<PerModel> per_model : register(b0);
StructuredBuffer<float3> input_positions : register(t0);
StructuredBuffer<float3> input_normals : register(t1);
StructuredBuffer<float4> input_tangents : register(t2);
StructuredBuffer<BoneWeights> skin : register(t3);
StructuredBuffer<Bone> bones : register(t4);
RWStructuredBuffer<float3> output_positions : register(u0);
RWStructuredBuffer<float3> output_normals : register(u1);
RWStructuredBuffer<float4> output_tangents : register(u2);

[numthreads(64, 1, 1)]
void main(in uint3 thread_id: SV_DispatchThreadID)
{
    uint index = thread_id.x;

    if (index >= per_model.num_of_vertices) {
        return;
    }
    
    // Get inputs.
    float3 position = input_positions[index];
    float3 normal = per_model.input_mesh_flags & MESH_FLAG_NORMAL ? input_normals[index] : float3(0, 0, 0);
    float4 tangent = per_model.input_mesh_flags & MESH_FLAG_TANGENT ? input_tangents[index] : float4(0, 0, 0, 0);

    // Morph targets.
    for (int i = 0; i < per_model.num_of_morph_targets; i++) {
        float weight = per_model.morph_targets[i].weight;
        if (per_model.morph_targets[i].position_descriptor != -1) {
            StructuredBuffer<float3> morph_positions = ResourceDescriptorHeap[per_model.morph_targets[i].position_descriptor];
            float3 morph_position = morph_positions[index];
            position += weight * morph_position;
        }
        if (per_model.morph_targets[i].normal_descriptor != -1) {
            StructuredBuffer<float3> morph_normals = ResourceDescriptorHeap[per_model.morph_targets[i].normal_descriptor];
            float3 morph_normal = morph_normals[index];
            normal += weight * morph_normals[index];
        }
        if (per_model.morph_targets[i].tangent_descriptor != -1) {
            StructuredBuffer<float3> morph_tangents = ResourceDescriptorHeap[per_model.morph_targets[i].tangent_descriptor];
            float3 morph_tangent = morph_tangents[index];
            tangent.xyz += weight * morph_tangents[index];
        }
    }

    // Skinning.
    if (per_model.input_mesh_flags & MESH_FLAG_JOINT_WEIGHT) {
        BoneWeights bone_weights;
        bone_weights = skin[index];
        
        // Positions.
        float3 skinned_position = float3(0., 0., 0.);
        for (int i = 0; i < 4; i++) {
            float4x4 transform = bones[bone_weights.bones[i]].transform;
            skinned_position += bone_weights.weights[i] * mul(transform, float4(position, 1.)).xyz;
        }
        position = skinned_position;
        
        // Normals.
        if (per_model.input_mesh_flags & MESH_FLAG_NORMAL) {
            float3 skinned_normal = float3(0., 0., 0.);
            for (int i = 0; i < 4; i++) {
                float4x4 transform = bones[bone_weights.bones[i]].inverse_transpose;
                skinned_normal += bone_weights.weights[i] * mul(transform, float4(normal, 0.)).xyz;
            }
            normal = skinned_normal;
        }

        // Tangents.
        if (per_model.input_mesh_flags & MESH_FLAG_TANGENT) {
            // Assume we don't change handedness.
            float3 skinned_tangent = float3(0., 0., 0.);
            for (int i = 0; i < 4; i++) {
                float4x4 transform = bones[bone_weights.bones[i]].inverse_transpose;
                skinned_tangent += bone_weights.weights[i] * mul(transform, float4(tangent.xyz, 0.)).xyz;
            }
            tangent.xyz = skinned_tangent;
        }
    }

    if (per_model.output_mesh_flags & DYNAMIC_MESH_FLAG_POSITION) {
        output_positions[index] = position;
    }
    if (per_model.output_mesh_flags & DYNAMIC_MESH_FLAG_NORMAL) {
        output_normals[index] = normalize(normal);
    }
    if (per_model.output_mesh_flags & DYNAMIC_MESH_FLAG_TANGENT) {
        output_tangents[index] = float4(normalize(tangent.xyz), tangent.w);
    }
}