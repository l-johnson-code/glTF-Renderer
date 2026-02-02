#pragma once

#include "Bloom.h"
#include "EnvironmentMap.h"
#include "ForwardPass.h"
#include "Gltf.h"

class Rasterizer {

    public:

    struct Settings {
		int transmission_downsample_sample_pattern = 1;
		float bloom_strength = 0.01f;
		int bloom_radius = 4;
		uint32_t render_flags;
	};

    struct ExecuteParams {
        Gltf* gltf = nullptr;
        int scene = 0;
        Camera* camera = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS gpu_materials = 0;
        D3D12_GPU_VIRTUAL_ADDRESS gpu_lights = 0;
        int light_count = 0;
        EnvironmentMap::Map* environment_map = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE output_rtv = {};
        ID3D12Resource* output_resource = nullptr;
    };

    void Init(ID3D12Device* device, RtvPool* rtv_allocator, DsvPool* dsv_allocator, CbvSrvUavPool* cbv_uav_srv_allocator, uint32_t width, uint32_t height);
    void Resize(uint32_t width, uint32_t height);
	void DrawScene(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* frame_allocator, CbvSrvUavStack* descriptor_allocator, const Settings* settings, const ExecuteParams* execute_params);
    void Shutdown();

    private:

    struct RenderObject {
		glm::mat4x4 transform;
		glm::mat4x4 normal_transform;
		glm::mat4x4 previous_transform;
		int mesh_id;
		int dynamic_mesh_id;
		int primitive_id;
		int material_id;
	};

	Microsoft::WRL::ComPtr<ID3D12Device> device;
    RtvPool* rtv_allocator;
    DsvPool* dsv_allocator;
    CbvSrvUavPool* cbv_uav_srv_allocator;

    uint32_t width;
    uint32_t height;
    glm::mat4x4 previous_world_to_clip;

    // Render targets and resolution dependent resources.
	static constexpr float DEPTH_CLEAR_VALUE = 0.0f;
    D3D12_CPU_DESCRIPTOR_HANDLE depth_dsv = {0};
    int depth_srv = -1;
	Microsoft::WRL::ComPtr<ID3D12Resource> depth;
    D3D12_CPU_DESCRIPTOR_HANDLE motion_vectors_rtv = {0};
    int motion_vectors_srv = -1;
	Microsoft::WRL::ComPtr<ID3D12Resource> motion_vectors;
    int transmission_srv = -1;
	Microsoft::WRL::ComPtr<ID3D12Resource> transmission;
    
    std::vector<RenderObject> opaque_render_objects;
	std::vector<RenderObject> alpha_mask_render_objects;
	std::vector<RenderObject> alpha_render_objects;
	std::vector<RenderObject> transparent_render_objects;

    ForwardPass forward;
    Bloom bloom;

    // Forward renderer.
	void SetViewportAndScissorRects(ID3D12GraphicsCommandList* command_list, int width, int height);
	void GatherRenderObjects(Gltf* gltf, int scene);
	void SortRenderObjects(glm::vec3 camera_pos);
	void DrawRenderObjects(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* frame_allocator, Gltf* gltf, const std::vector<RenderObject>& render_objects);
};