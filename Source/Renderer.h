#pragma once

#include <cassert>

#include <directx/d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Camera.h"
#include "EnvironmentMap.h"
#include "Gltf.h"
#include "GpuResources.h"
#include "GpuSkin.h"
#include "MultiBuffer.h"
#include "Pathtracer.h"
#include "Rasterizer.h"
#include "RayTracingAccelerationStructure.h"
#include "Swapchain.h"
#include "ToneMapper.h"
#include "UploadBuffer.h"

class Renderer {
public:

	enum RendererType {
		RENDERER_TYPE_RASTERIZER,
		RENDERER_TYPE_PATHTRACER,
	};

	struct RenderSettings {
		RendererType renderer_type;
		int width = 800;
		int height = 600;
		int vsync_interval = 1;
		int anisotropic_filtering = 0;
		ToneMapper::Config tone_mapper_config;
		Rasterizer::Settings raster;
		Pathtracer::Settings pathtracer;
	};

	Microsoft::WRL::ComPtr<ID3D12Device5> device;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> graphics_command_list;
	GpuResources resources;
	UploadBuffer upload_buffer;
	EnvironmentMap environment_map;

	bool Init(HWND window, RenderSettings* settings);
	void DrawFrame(Gltf* gltf, int scene, Camera* camera, RenderSettings* render_settings);
	void Destroy();
	void WaitForOutstandingWork();

private:

	struct GpuLight {
		enum Type {
            TYPE_POINT,
            TYPE_SPOT,
            TYPE_DIRECTIONAL,
        };
		int type;
		glm::vec3 position;
		float cutoff;
		glm::vec3 direction;
		float intensity;
		glm::vec3 color;
		float inner_angle;
		float outer_angle;
		std::byte pad[8];
	};

	struct TextureSample {
		int descriptor = -1;
        int sampler = 0;
        int tex_coord = 0;
        float rotation = 0.0f;
        glm::vec2 offset = glm::vec2(0.0f);
        glm::vec2 scale = glm::vec2(1.0f);
        TextureSample() = default;
        TextureSample(const Gltf::Material::Texture& texture) {
            this->descriptor = texture.texture;
            this->sampler = texture.sampler;
            this->tex_coord = texture.tex_coord;
            this->rotation = texture.rotation;
            this->offset = texture.offset;
            this->scale = texture.scale;
        }
    };

    struct GpuMaterial {
        uint32_t flags;
        int alpha_mode;
        float metalness_factor;
        float roughness_factor;
        glm::vec4 base_color_factor;
        float occlusion_factor;
        glm::vec3 emissive_factor;
        float alpha_cutoff;
        float ior;
        float normal_scale;
        alignas(16) TextureSample normal;
        alignas(16) TextureSample albedo;
        alignas(16) TextureSample metallic_roughness;
        alignas(16) TextureSample occlusion;
        alignas(16) TextureSample emissive;
        float specular_factor;
        glm::vec3 specular_color_factor;
        alignas(16) TextureSample specular;
        alignas(16) TextureSample specular_color;
        float clearcoat_factor;
        float clearcoat_roughness_factor;
        float clearcoat_normal_scale;
        std::byte pad_1[4];
        alignas(16) TextureSample clearcoat;
        alignas(16) TextureSample clearcoat_roughness;
        alignas(16) TextureSample clearcoat_normal;
        float anisotropy_strength;
        float anisotropy_rotation;
        std::byte pad_2[8];
        alignas(16) TextureSample anisotropy_texture;
        glm::vec3 sheen_color_factor;
        float sheen_roughness_factor;
        alignas(16) TextureSample sheen_color_texture;
        alignas(16) TextureSample sheen_roughness_texture;
        float transmission_factor = 0;
        float thickness_factor = 0;
        std::byte pad_3[8];
        alignas(16) TextureSample transmission_texture;
        float attenuation_distance = 0;
        glm::vec3 attenuation_color = glm::vec3(1.0, 1.0, 1.0);
        alignas(16) TextureSample thickness_texture;
        GpuMaterial() = default;
        GpuMaterial(const Gltf::Material& material) {
            this->flags = material.flags;
			this->alpha_mode = material.alpha_mode;
            this->metalness_factor = material.metalness_factor;
            this->roughness_factor = material.roughness_factor;
            this->occlusion_factor = material.occlusion_factor;
            this->emissive_factor = material.emissive_strength * material.emissive_factor;
            this->base_color_factor = material.base_color_factor;
            this->normal_scale = material.normal_map_scale;
            this->normal = TextureSample(material.normal);
            this->albedo = TextureSample(material.albedo);
            this->metallic_roughness = TextureSample(material.metallic_roughness);
            this->occlusion = TextureSample(material.occlusion);
            this->emissive = TextureSample(material.emissive);
            this->alpha_cutoff = material.alpha_mode == Gltf::Material::ALPHA_MODE_MASK ? material.alpha_cutoff : 0.0f;
            this->ior = material.ior;
            this->specular_color_factor = material.specular_color_factor;
            this->specular_factor = material.specular_factor;
            this->specular = TextureSample(material.specular_texture);
            this->specular_color = TextureSample(material.specular_color_texture);
            this->clearcoat_factor = material.clearcoat_factor;
            this->clearcoat_roughness_factor = material.clearcoat_roughness_factor;
            this->clearcoat_normal_scale = material.clearcoat_normal_scale;
            this->clearcoat = TextureSample(material.clearcoat_texture);
            this->clearcoat_roughness = TextureSample(material.clearcoat_roughness_texture);
            this->clearcoat_normal = TextureSample(material.clearcoat_normal_texture);
            this->anisotropy_strength = material.anisotropy_strength;
            this->anisotropy_rotation = material.anisotropy_rotation;
            this->anisotropy_texture = TextureSample(material.anisotropy_texture);
            this->sheen_color_factor = material.sheen_color_factor;
            this->sheen_roughness_factor = material.sheen_roughness_factor;
            this->sheen_color_texture = TextureSample(material.sheen_color_texture);
            this->sheen_roughness_texture = TextureSample(material.sheen_roughness_texture);
            this->transmission_factor = material.transmission_factor;
            this->transmission_texture = TextureSample(material.transmission_texture);
            this->thickness_factor = material.thickness_factor;
            this->attenuation_distance = material.attenuation_distance;
            this->attenuation_color = material.attenuation_color;
            this->thickness_texture = TextureSample(material.thickness_texture);
        }
    };

	// Feature support.
	bool raytracing_tier_1_1_supported = false;
	bool gpu_upload_heaps_supported = false;

	uint32_t display_width;
	uint32_t display_height;

	RenderSettings settings;

	// Render targets and resolution dependent resources.
	Microsoft::WRL::ComPtr<ID3D12Resource> display;
	int display_rtv = -1;
	int display_uav = -1;
	
	const int MAX_LIGHTS = 10;
	std::vector<GpuLight> lights;
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> raytracing_instances;
	D3D12_GPU_VIRTUAL_ADDRESS gpu_lights;
	D3D12_GPU_VIRTUAL_ADDRESS gpu_materials;

	uint64_t frame = 0;

	MultiBuffer<std::vector<Microsoft::WRL::ComPtr<IUnknown>>, Config::FRAME_COUNT> deferred_release;

	Swapchain swapchain;
	MultiBuffer<CpuMappedLinearBuffer, Config::FRAME_COUNT> frame_allocators;
	GpuSkin gpu_skinner;
	Rasterizer rasterizer;
	Pathtracer pathtracer;

	// Command submission.
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> graphics_command_queue;
	MultiBuffer<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, Config::FRAME_COUNT> graphics_command_allocators;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	MultiBuffer<int, Config::FRAME_COUNT> fence_values;
	uint64_t current_frame = 0;
	HANDLE frame_event = nullptr;

	// Pipelines.
	ToneMapper tone_mapper;
	EnvironmentMap::Map map;
	bool environment_map_loaded = false;

	void CreateRenderTargets();
	void CreateRendererTypeSpecificResources(RendererType renderer_type);
	void DestroyRendererTypeSpecificResources(RendererType renderer_type);

	void WaitForNextFrame();
	void EndFrame();

	// Skinning.
	void PerformSkinning(Gltf* gltf, int scene, CpuMappedLinearBuffer* frame_allocator);

	// UI.
	void InitializeImGui();
	void DrawImGui();

	void SetViewportAndScissorRects(ID3D12GraphicsCommandList* command_list, int width, int height);

	// Gather scene data to upload to GPU.
	void GatherLights(Gltf* gltf, int scene, CpuMappedLinearBuffer* allocator);
	void GatherMaterials(Gltf* gltf, CpuMappedLinearBuffer* allocator);

	void ApplySettingsChanges(const RenderSettings* new_settings);
};
