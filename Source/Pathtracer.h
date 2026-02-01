#pragma once

#include <cassert>

#include <directx/d3d12.h>
#include <glm/glm.hpp>
#include <wrl.h>

#include "BufferAllocator.h"
#include "EnvironmentMap.h"
#include "Gltf.h"
#include "ShaderTableBuilder.h"
#include "UploadBuffer.h"

class Pathtracer {

    public:

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
        DEBUG_OUTPUT_COUNT,
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
    };

    struct Settings {
		int min_bounces = 2;
		int max_bounces = 2;
		bool reset = false;
		int debug_output = Pathtracer::DEBUG_OUTPUT_NONE;
		uint32_t flags = Pathtracer::FLAG_ACCUMULATE | Pathtracer::FLAG_POINT_LIGHTS | Pathtracer::FLAG_ENVIRONMENT_MAP;
		glm::vec3 environment_color;
		float environment_intensity = 1;
		bool use_frame_as_seed = true;
		uint32_t seed = 0;
		float luminance_clamp = 1000.0;
		float min_russian_roulette_continue_prob = 0.1;
		float max_russian_roulette_continue_prob = 0.9;
		int max_accumulated_frames = 65536;
		float max_ray_length = 1000.0;
	};

    struct ExecuteParams {
        Gltf* gltf = nullptr;
        int scene = 0;
        Camera* camera = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t frame = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpu_materials = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpu_lights = 0;
        int light_count = 0;;
        EnvironmentMap::Map* environment_map = nullptr;
        int output_descriptor = -1;
        ID3D12Resource* output_resource = nullptr;
    };

    static constexpr int MAX_BOUNCES = 5;
    
    void Init(ID3D12Device5* device, UploadBuffer* upload_buffer);
	void PathtraceScene(ID3D12GraphicsCommandList4* command_list, CpuMappedLinearBuffer* frame_allocator, DescriptorStack* descriptor_allocator, const Settings* settings, const ExecuteParams* execute_params);
    void Shutdown();
    
    private:
    
    enum RootArguments {
        ROOT_PARAMETER_CONSTANT_BUFFER,
        ROOT_PARAMETER_ACCELERATION_STRUCTURE,
        ROOT_PARAMETER_INSTANCES,
        ROOT_PARAMETER_MATERIALS,
        ROOT_PARAMETER_LIGHTS,
        ROOT_PARAMETER_COUNT,
    };

    enum HitGroup {
        HIT_GROUP_BOUNCE,
        HIT_GROUP_SHADOW,
        HIT_GROUP_COUNT,
    };

    enum MissShader {
        MISS_SHADER_BOUNCE,
        MISS_SHADER_SHADOW,
        MISS_SHADER_COUNT,
    };

    struct GpuMeshInstance {
		glm::mat4x4 transform;
		glm::mat4x4 normal_transform;
		int index_descriptor = -1;
		int position_descriptor = -1;
		int normal_descriptor = -1;
		int tangent_descriptor = -1;
		int texcoord_descriptors[2] = {-1, -1};
		int color_descriptor = -1;
		int material_id = 0;
	};
    
    ShaderTableCollection shader_tables;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12StateObject> state_object;
    Microsoft::WRL::ComPtr<ID3D12Resource> shader_tables_resource;

	RaytracingAccelerationStructure acceleration_structure;

    std::vector<GpuMeshInstance> mesh_instances;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_mesh_instances;
    
    glm::mat4x4 previous_world_to_clip;
    int accumulated_frames = 0;

    void BuildAllBlas(Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list);
	void UpdateAllBlas(Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list);
	void BuildTlas(Gltf* gltf, int scene_id, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list, CpuMappedLinearBuffer* allocator);
};