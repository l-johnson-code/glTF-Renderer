#pragma once

#include "Bloom.h"
#include "EnvironmentMap.h"
#include "ForwardPass.h"
#include "Gltf.h"
#include "GpuResources.h"

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
        GpuResources::Texture* output;
    };

    void Init(ID3D12Device* device, GpuResources* gpu_resources, uint16_t width, uint16_t height);
    void Resize(uint16_t width, uint16_t height);
	void DrawScene(CommandContext* context, const Settings* settings, const ExecuteParams* execute_params);
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
    GpuResources* gpu_resources = nullptr;

    uint32_t width;
    uint32_t height;
    glm::mat4x4 previous_world_to_clip;

    // Render targets and resolution dependent resources.
    GpuResources::Texture depth;
    GpuResources::Texture motion_vectors;
	GpuResources::Texture transmission;
    
    std::vector<RenderObject> opaque_render_objects;
	std::vector<RenderObject> alpha_mask_render_objects;
	std::vector<RenderObject> alpha_render_objects;
	std::vector<RenderObject> transparent_render_objects;

    ForwardPass forward;
    Bloom bloom;

    // Forward renderer.
	void SetViewportAndScissorRects(CommandContext* context, int width, int height);
	void GatherRenderObjects(Gltf* gltf, int scene);
	void SortRenderObjects(glm::vec3 camera_pos);
	void DrawRenderObjects(CommandContext* context, Gltf* gltf, const std::vector<RenderObject>& render_objects);
};