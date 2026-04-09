#include "Common.hlsli"
#include "Vertex.hlsli"

enum MeshFlags {
    MESH_FLAG_INDEX = 1 << 0,
    MESH_FLAG_TANGENT_SPACE = 1 << 1,
    MESH_FLAG_TEXCOORD_0 = 1 << 2,
    MESH_FLAG_TEXCOORD_1 = 1 << 3,
    MESH_FLAG_COLOR = 1 << 4,
    MESH_FLAG_JOINT_WEIGHT = 1 << 5,
};

enum DynamicMeshFlags {
    DYNAMIC_MESH_FLAG_POSITION = 1 << 0,
    DYNAMIC_MESH_FLAG_TANGENT_SPACE = 1 << 1,
};

enum MorphFlags {
    MORPH_FLAG_POSITIONS = 1 << 0,
    MORPH_FLAG_TANGENT_SPACE = 1 << 1,
};

struct BoneWeights {
    uint32_t bones[2];
    uint32_t weights[2];
};

struct PerModel {
    uint32_t num_of_vertices;
    uint32_t input_mesh_flags;
    uint32_t output_mesh_flags;
    int num_of_morph_targets;
    struct {
        float weight;
        int position_descriptor;
        int tangent_space_descriptor;
    } morph_targets[4];
};

struct Bone {
    float4x4 transform;
    float4x4 inverse_transpose;
};

ConstantBuffer<PerModel> per_model : register(b0);
StructuredBuffer<float3> input_positions : register(t0);
StructuredBuffer<uint> input_tangent_space : register(t1);
StructuredBuffer<BoneWeights> skin : register(t3);
StructuredBuffer<Bone> bones : register(t4);
RWStructuredBuffer<float3> output_positions : register(u0);
RWStructuredBuffer<uint> output_tangent_space : register(u1);

[numthreads(64, 1, 1)]
void main(in uint3 thread_id: SV_DispatchThreadID)
{
    uint index = thread_id.x;

    if (index >= per_model.num_of_vertices) {
        return;
    }
    
    // Get inputs.
    float3 position = input_positions[index];
    float3 normal = float3(0, 0, 0);
    float4 tangent = float4(0, 0, 0, 1);
    if (per_model.input_mesh_flags & MESH_FLAG_TANGENT_SPACE) {
        DecodeTangentSpace(UnpackR10G10B10A2(input_tangent_space[index]), normal, tangent);
    }

    // Morph targets.
    for (int i = 0; i < per_model.num_of_morph_targets; i++) {
        float weight = per_model.morph_targets[i].weight;
        if (per_model.morph_targets[i].position_descriptor != -1) {
            Buffer<float3> morph_pos_buffer = ResourceDescriptorHeap[per_model.morph_targets[i].position_descriptor];
            float3 morph_position = morph_pos_buffer[index];
            position += weight * morph_position;
        }
        if (per_model.morph_targets[i].tangent_space_descriptor != -1) {
            Buffer<float4> morph_tangent_space_buffer = ResourceDescriptorHeap[per_model.morph_targets[i].tangent_space_descriptor];
            float4 morph_tangent_space = morph_tangent_space_buffer[index];
            float3 morph_normal;
            float4 morph_tangent;
            DecodeTangentSpace(morph_tangent_space, morph_normal, morph_tangent);
            normal += weight * morph_normal;
            tangent.xyz += weight * morph_tangent.xyz;
        }
    }

    // Skinning.
    if (per_model.input_mesh_flags & MESH_FLAG_JOINT_WEIGHT) {
        BoneWeights bone_weights;
        bone_weights = skin[index];

        // Unpack the bone weights.
        uint32_t bone_ids[4];
        float weights[4];
        for (int i = 0; i < 2; i++) {
            bone_ids[2 * i] = bone_weights.bones[i] & 0xffff;
            bone_ids[2 * i + 1] = bone_weights.bones[i] >> 16;
            weights[2 * i] = (float)(bone_weights.weights[i] & 0xffff) / 65535.0f;
            weights[2 * i + 1] = (float)(bone_weights.weights[i] >> 16) / 65535.0f;
        }
        
        // Positions.
        float3 skinned_position = float3(0., 0., 0.);
        for (int i = 0; i < 4; i++) {
            float4x4 transform = bones[bone_ids[i]].transform;
            skinned_position += weights[i] * mul(transform, float4(position, 1.)).xyz;
        }
        position = skinned_position;
        
        // Normals and tangents.
        if (per_model.input_mesh_flags & MESH_FLAG_TANGENT_SPACE) {
            float3 skinned_normal = float3(0., 0., 0.);
            for (int i = 0; i < 4; i++) {
                float4x4 transform = bones[bone_ids[i]].inverse_transpose;
                skinned_normal += weights[i] * mul(transform, float4(normal, 0.)).xyz;
            }
            normal = skinned_normal;
            // Assume we don't change handedness.
            float3 skinned_tangent = float3(0., 0., 0.);
            for (int i = 0; i < 4; i++) {
                float4x4 transform = bones[bone_ids[i]].transform;
                skinned_tangent += weights[i] * mul(transform, float4(tangent.xyz, 0.)).xyz;
            }
            tangent.xyz = skinned_tangent;
        }
    }

    if (per_model.output_mesh_flags & DYNAMIC_MESH_FLAG_POSITION) {
        output_positions[index] = position;
    }
    if (per_model.output_mesh_flags & DYNAMIC_MESH_FLAG_TANGENT_SPACE) {
        output_tangent_space[index] = EncodeTangentSpace(normalize(normal), float4(normalize(tangent.xyz), tangent.w));
    }
}