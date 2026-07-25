#pragma once

#include <cassert>

#include <directx/d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Camera.h"
#include "EnvironmentMap.h"
#include "Gltf.h"
#include "GpuResources.h"
#include "GpuScene.h"
#include "GpuSkin.h"
#include "MultiBuffer.h"
#include "Pathtracer.h"
#include "Rasterizer.h"
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
	Gpu::Resources resources;
	UploadBuffer upload_buffer;
	EnvironmentMap environment_map;
    EnvironmentMap::Map map;

	bool Init(HWND window, RenderSettings* settings);
	void DrawFrame(Gltf* gltf, Camera* camera, RenderSettings* render_settings);
	void Destroy();
	void WaitForOutstandingWork();

private:

	// Feature support.
	bool raytracing_tier_1_1_supported = false;
	bool gpu_upload_heaps_supported = false;

	uint32_t display_width;
	uint32_t display_height;

	RenderSettings settings;

	// Render targets and resolution dependent resources.
    Gpu::Texture display;
	    
    // Lookup tables.
	Gpu::Texture sheen_e;

	uint64_t frame = 0;

	MultiBuffer<std::vector<Gpu::Texture>, Config::FRAME_COUNT> deferred_release;

	GpuScene gpu_scene;

	Swapchain swapchain;
	MultiBuffer<CpuMappedLinearBuffer, Config::FRAME_COUNT> frame_allocators;
	GpuSkin gpu_skinner;
	Rasterizer rasterizer;
	Pathtracer pathtracer;

	// Command submission.
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> graphics_command_queue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> graphics_command_list;
	MultiBuffer<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>, Config::FRAME_COUNT> graphics_command_allocators;
	std::vector<D3D12_RESOURCE_BARRIER> resource_barriers;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	MultiBuffer<int, Config::FRAME_COUNT> fence_values;
	uint64_t current_frame = 0;
	HANDLE frame_event = nullptr;

	// Pipelines.
	ToneMapper tone_mapper;
	bool environment_map_loaded = false;

    void LoadLookupTables(UploadBuffer* upload_buffer);
	void CreateRenderTargets();
	void CreateRendererTypeSpecificResources(RendererType renderer_type);
	void DestroyRendererTypeSpecificResources(RendererType renderer_type);

	void WaitForNextFrame();
	void EndFrame();

	// Skinning.
	void PerformSkinning(CommandContext* context, Gltf* gltf);

	// UI.
	void InitializeImGui();
	void DrawImGui();

	void SetViewportAndScissorRects(ID3D12GraphicsCommandList* command_list, int width, int height);

	void ApplySettingsChanges(const RenderSettings* new_settings);
};
