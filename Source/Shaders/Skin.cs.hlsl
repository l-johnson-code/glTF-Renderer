#include "Common.hlsli"
#include "Vertex.hlsli"

enum MeshFlags {
    MESH_FLAG_JOINT_WEIGHT = 1 << 4,
};

enum MorphFlags {
    MORPH_FLAG_POSITION = 1 << 0,
    MORPH_FLAG_NORMAL = 1 << 1,
    MORPH_FLAG_TANGENT = 1 << 2,
};

struct BoneWeights {
    uint32_t bones[2];
    uint32_t weights[2];
};

struct PositionAndTangentSpace {
    float3 position;
    uint tangent_space;
};

struct PerModel {
    uint32_t num_of_vertices;
    uint32_t input_mesh_flags;
    uint32_t output_mesh_flags;
    int input_position_and_tangent_space_descriptor;
    int skin_descriptor;
    int bones_descriptor;
    int output_position_and_tangent_space_descriptor;
    int num_of_morph_targets;
    struct {
        float weight;
        int position_and_tangent_space_descriptor;
        uint32_t flags;
    } morph_targets[4];
};

struct Bone {
    float4x4 transform;
    float4x4 inverse_transpose;
};

ConstantBuffer<PerModel> g_per_model : register(b0);

[numthreads(64, 1, 1)]
void main(in uint3 thread_id: SV_DispatchThreadID)
{
    uint index = thread_id.x;

    if (index >= g_per_model.num_of_vertices) {
        return;
    }
    
    // Get inputs.
    StructuredBuffer<PositionAndTangentSpace> input_position_and_tangent_space = ResourceDescriptorHeap[g_per_model.input_position_and_tangent_space_descriptor];
    PositionAndTangentSpace position_and_tangent_space = input_position_and_tangent_space[index];
    float3 position = position_and_tangent_space.position;
    float3 normal = float3(0, 0, 0);
    float4 tangent = float4(0, 0, 0, 1);
    DecodeTangentSpace(UnpackR10G10B10A2(position_and_tangent_space.tangent_space), normal, tangent);

    // Morph targets.
    for (int i = 0; i < g_per_model.num_of_morph_targets; i++) {
        float weight = g_per_model.morph_targets[i].weight;
        if (g_per_model.morph_targets[i].position_and_tangent_space_descriptor != -1) {
            StructuredBuffer<PositionAndTangentSpace> morph_buffer = ResourceDescriptorHeap[g_per_model.morph_targets[i].position_and_tangent_space_descriptor];
            PositionAndTangentSpace morph_data = morph_buffer[index];
            if (g_per_model.morph_targets[i].flags & MORPH_FLAG_POSITION) {
                position += weight * morph_data.position;
            }
            float3 morph_normal;
            float4 morph_tangent;
            DecodeTangentSpace(morph_data.tangent_space, morph_normal, morph_tangent);
            if (g_per_model.morph_targets[i].flags & MORPH_FLAG_NORMAL) {
                normal += weight * morph_normal;
            }
            if (g_per_model.morph_targets[i].flags & MORPH_FLAG_TANGENT) {
                tangent.xyz += weight * morph_tangent.xyz;
            }
        }
    }

    // Skinning.
    if (g_per_model.input_mesh_flags & MESH_FLAG_JOINT_WEIGHT) {
        StructuredBuffer<Bone> bones = ResourceDescriptorHeap[g_per_model.bones_descriptor];
        StructuredBuffer<BoneWeights> skin = ResourceDescriptorHeap[g_per_model.skin_descriptor];
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

    RWStructuredBuffer<PositionAndTangentSpace> output_position_and_tangent_space = ResourceDescriptorHeap[g_per_model.output_position_and_tangent_space_descriptor];
    output_position_and_tangent_space[index].position = position;
    output_position_and_tangent_space[index].tangent_space = EncodeTangentSpace(normalize(normal), float4(normalize(tangent.xyz), tangent.w));
}