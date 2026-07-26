#pragma once

#include "Bloom.h"
#include "EnvironmentMap.h"
#include "ForwardPass.h"
#include "Scene.h"
#include "GpuResources.h"
#include "GpuScene.h"

class Rasterizer {

    public:

    struct Settings {
        bool frustum_culling = true;
        bool draw_bounding_boxes = false;
		int transmission_downsample_sample_pattern = 1;
		float bloom_strength = 0.01f;
		int bloom_radius = 4;
		uint32_t render_flags;
	};

    struct ExecuteParams {
        Scene* scene = nullptr;
        Camera* camera = nullptr;
        GpuScene* gpu_scene = nullptr;
        EnvironmentMap::Map* environment_map = nullptr;
        Gpu::Texture* output;
    };

    void Init(Gpu::Resources* gpu_resources, uint16_t width, uint16_t height);
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

    Gpu::Resources* gpu_resources = nullptr;

    uint32_t width;
    uint32_t height;
    glm::mat4x4 previous_world_to_clip;

    // Render targets and resolution dependent resources.
    Gpu::Texture depth;
    Gpu::Texture motion_vectors;
	Gpu::Texture transmission;
    
    std::vector<RenderObject> opaque_render_objects;
	std::vector<RenderObject> alpha_mask_render_objects;
	std::vector<RenderObject> alpha_render_objects;
	std::vector<RenderObject> transparent_render_objects;

    ForwardPass forward;
    Bloom bloom;

    // Forward renderer.
	void SetViewportAndScissorRects(CommandContext* context, int width, int height);
	void GatherRenderObjects(Scene* scene, glm::mat4x4 world_to_clip, const Settings* settings);
	void SortRenderObjects(glm::vec3 camera_pos);
	void DrawRenderObjects(CommandContext* context, Scene* scene, const std::vector<RenderObject>& render_objects);
};