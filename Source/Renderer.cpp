#include "Renderer.h"

#include <algorithm>

#include <directx/d3d12.h>
#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/dxgiformat.h>
#include <imgui/backends/imgui_impl_dx12.h>
#include <spdlog/spdlog.h>
#include <tinyexr/tinyexr.h>

#include "BufferAllocator.h"
#include "Config.h"
#include "DescriptorAllocator.h"
#include "DirectXHelpers.h"
#include "GpuResources.h"
#include "GpuSkin.h"
#include "Profiling.h"

bool Renderer::Init(HWND window, RenderSettings* settings)
{
	HRESULT result = S_OK;

	this->settings = *settings;

	this->display_width = settings->width;
	this->display_height = settings->height;

	this->current_frame = 0;

	// Enable debug layer.
	if (::Config::enable_d3d12_debug_layer) {
		Microsoft::WRL::ComPtr<ID3D12Debug> debug_0;
		result = D3D12GetDebugInterface(IID_PPV_ARGS(debug_0.GetAddressOf()));
		if (result == S_OK) {
			debug_0->EnableDebugLayer();
			SPDLOG_INFO("DirectX debug layer is enabled.");
		} else {
			SPDLOG_WARN("Failed to enable the DirectX debug layer.");
		}
	}

	// Enable GPU based validation.
	if (::Config::enable_gpu_based_validation) {
		Microsoft::WRL::ComPtr<ID3D12Debug1> debug_1;
		result = D3D12GetDebugInterface(IID_PPV_ARGS(debug_1.GetAddressOf()));
		if (result == S_OK) {
			debug_1->SetEnableGPUBasedValidation(true);
			SPDLOG_INFO("GPU based validation is enabled.");
		} else {
			SPDLOG_WARN("Failed to enable GPU based validation.");
		}
	}
	
	// Create the device.
	result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
	if (result != S_OK) {
		SPDLOG_ERROR("Failed to create DirectX 12 device.");
		return false;
	}

	// Filter out some debug messages.
	if (::Config::enable_d3d12_debug_layer) {
		Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
		result = device.As(&queue);
		assert(result != E_INVALIDARG);
		if (result == S_OK) {
			D3D12_MESSAGE_ID messages[] = {
				D3D12_MESSAGE_ID_COMMAND_LIST_DRAW_VERTEX_BUFFER_NOT_SET 
			};
			D3D12_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = std::size(messages);
			filter.DenyList.pIDList = messages;
			result = queue->AddStorageFilterEntries(&filter);
			assert(result != E_INVALIDARG);
		}
	}

	// Check feature levels.
	// We need shader model 6.6 so that we can use ResourceDescriptorHeap and SamplerDescriptorHeap for bindless rendering.
	D3D12_FEATURE_DATA_SHADER_MODEL shader_model = {D3D_SHADER_MODEL_6_6};
	result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model));
	if (result == S_OK && shader_model.HighestShaderModel == D3D_SHADER_MODEL_6_6) {
		SPDLOG_INFO("Shader model 6.6 or higher is supported.");
	} else if (result == E_INVALIDARG) {
		SPDLOG_INFO("Unable to determine shader model 6.6 support.");
	} else {
		SPDLOG_INFO("Shader model 6.6 or higher is not supported.");
		return false;
	}
	D3D12_FEATURE_DATA_D3D12_OPTIONS16 gpu_upload_heaps = {};
	result = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &gpu_upload_heaps, sizeof(gpu_upload_heaps));
	if (gpu_upload_heaps.GPUUploadHeapSupported == true) {
		gpu_upload_heaps_supported = true;
		SPDLOG_INFO("GPU upload heaps are supported.");
	} else {
		SPDLOG_INFO("GPU upload heaps are not supported.");
	}
	// Check we can use the path tracer.
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 raytracing_tier = {};
	result = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &raytracing_tier, sizeof(raytracing_tier));
	if (raytracing_tier.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1) {
		raytracing_tier_1_1_supported = true;
		SPDLOG_INFO("Raytracing tier 1.1 is supported.");
	} else {
		SPDLOG_INFO("Raytracing tier 1.1 is not supported.");
	}

	this->resources.Create(this->device.Get());

	// Create the command queue.
	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	result = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(graphics_command_queue.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	if (result != S_OK) {
		SPDLOG_ERROR("Failed to create command queue.");
		return false;
	}
	SetName(graphics_command_queue.Get(), "Graphics Command Queue");

	// Create command allocators.
	for (int i = 0; i < ::Config::FRAME_COUNT; i++) {
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(graphics_command_allocators[i].ReleaseAndGetAddressOf()));
		assert(result == S_OK);
		if (result != S_OK) {
			SPDLOG_ERROR("Failed to create command allocator.");
			return false;
		}
		SetName(graphics_command_allocators[i].Get(), "Graphics Command Allocator");
	}

	// Create the command list.
	result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, graphics_command_allocators.Current().Get(), nullptr, IID_PPV_ARGS(graphics_command_list.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	if (result != S_OK) {
		SPDLOG_ERROR("Failed to create command list.");
		return false;
	}
	SetName(graphics_command_list.Get(), "Graphics Command List");

	// Create frame fence.
	result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(this->fence.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	SetName(this->fence.Get(), "Frame Fence");
	this->frame_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	// Create the swapchain.
	swapchain.Create(this->device.Get(), this->graphics_command_queue.Get(), &this->resources.rtv_allocator, window, display_width, display_height);
	
	upload_buffer.Create(this->device.Get(), &this->resources, ::Config::UPLOAD_BUFFER_CAPACITY, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, ::Config::FRAME_COUNT);

	for (int i = 0; i < frame_allocators.Size(); i++) {
		Gpu::BufferDesc buffer_desc = {
			.size = ::Config::FRAME_HEAP_CAPACITY,
			.flags = Gpu::BUFFER_FLAG_PERSISTENT_MAP,
			.heap_type = resources.allocator.SupportsGpuUploadHeap() ? D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_UPLOAD,
			.name = "Transient Resources",
		};
		frame_allocators[i].Create(&this->resources, &buffer_desc);
	}

	InitializeImGui();

	upload_buffer.Begin();

	CreateRenderTargets();
	gpu_skinner.Create(&this->resources);
	tone_mapper.Create(&this->resources);
	environment_map.Init(&this->resources);
	LoadLookupTables(&this->upload_buffer);

	if (settings->renderer_type == RENDERER_TYPE_RASTERIZER) {
		rasterizer.Init(&this->resources, this->display_width, this->display_height);
	} else {
		pathtracer.Init(this->device.Get(), &this->resources, &this->upload_buffer, this->display_width, this->display_height);
	}

	uint64_t submission_id = upload_buffer.Submit();
	upload_buffer.WaitForSubmissionToComplete(submission_id);

	result = graphics_command_list->Close();
	assert(result == S_OK);
	result = this->graphics_command_queue->Signal(fence.Get(), current_frame);
	assert(result == S_OK);
	fence_values.Current() = current_frame;
	frame++;

	return true;
}

void Renderer::InitializeImGui()
{
	ImGui_ImplDX12_InitInfo imgui;
	imgui.Device = this->device.Get();
    imgui.CommandQueue = this->graphics_command_queue.Get(); 
    imgui.NumFramesInFlight = ::Config::FRAME_COUNT;
    imgui.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;   
    imgui.DSVFormat = DXGI_FORMAT_D32_FLOAT;         
    imgui.UserData = &resources.cbv_uav_srv_dynamic_allocator;
    imgui.SrvDescriptorHeap = resources.cbv_uav_srv_dynamic_allocator.DescriptorHeap();
    imgui.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle) {
		DescriptorAllocator* descriptor_pool = (DescriptorAllocator*)info->UserData;
		int descriptor = descriptor_pool->Allocate(1);
		*out_cpu_desc_handle = descriptor_pool->GetCpuHandle(descriptor);
		*out_gpu_desc_handle = descriptor_pool->GetGpuHandle(descriptor);
	};
    imgui.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle) {
		DescriptorAllocator* descriptor_pool = (DescriptorAllocator*)info->UserData;
		int descriptor = descriptor_pool->GetIndex(cpu_desc_handle);
		descriptor_pool->Free(descriptor);
	};
	ImGui_ImplDX12_Init(&imgui);
}

void Renderer::DrawImGui()
{
	// Draw user interface.
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), this->graphics_command_list.Get());
}

void Renderer::WaitForNextFrame()
{
	ProfileZoneScoped();
	// Wait until we are ready to render.
	current_frame++;
	uint64_t completed_frame = fence->GetCompletedValue();
	int fence_value = fence_values.Current();
	if (completed_frame < fence_value) {
		HRESULT result = fence->SetEventOnCompletion(fence_value, frame_event);
		assert(result == S_OK);
		WaitForSingleObjectEx(frame_event, INFINITE, FALSE);
	}
}

void Renderer::ApplySettingsChanges(const Renderer::RenderSettings* new_settings)
{
	bool recreate_render_targets = false;

	// Change renderer.
	if (new_settings->renderer_type != this->settings.renderer_type) {
		WaitForOutstandingWork();
		if (this->settings.renderer_type == RENDERER_TYPE_RASTERIZER) {
			rasterizer.Shutdown();
		} else {
			pathtracer.Shutdown();
		}
		if (new_settings->renderer_type == RENDERER_TYPE_RASTERIZER) {
			rasterizer.Init(&this->resources, new_settings->width, new_settings->height);
		} else {
			this->upload_buffer.Begin();
			pathtracer.Init(this->device.Get(), &this->resources, &this->upload_buffer, new_settings->width, new_settings->height);
			uint64_t submission = this->upload_buffer.Submit();
			this->upload_buffer.WaitForSubmissionToComplete(submission);
		}
		recreate_render_targets = true;
	}

	// Set display resolution.
	if (new_settings->width != this->settings.width || new_settings->height != this->settings.height) {
		WaitForOutstandingWork();
		this->display_width = new_settings->width;
		this->display_height = new_settings->height;
		this->swapchain.Resize(this->device.Get(), this->display_width, this->display_height);
		recreate_render_targets = true;
	}
		
	this->settings = *new_settings;
	
	// Set display settings.
	if (recreate_render_targets) {
		CreateRenderTargets();
		if (new_settings->renderer_type == this->settings.renderer_type) {
			if (this->settings.renderer_type == RENDERER_TYPE_RASTERIZER) {
				rasterizer.Resize(this->display_width, this->display_height);
			} else {
				pathtracer.Resize(this->display_width, this->display_height);
			}
		}
	}
}

void Renderer::DrawFrame(Gltf* gltf, int scene, Camera* camera, RenderSettings* settings)
{
	// Apply any settings changes that require a pipeline flush, such as changing resolution.
	ApplySettingsChanges(settings);
	
	// Wait for pending work to be completed.
	WaitForNextFrame();
	this->graphics_command_allocators.Current()->Reset();
	this->graphics_command_list->Reset(this->graphics_command_allocators.Current().Get(), nullptr);

	// Release any resources.
	deferred_release.Next();
	deferred_release.Current().clear();

	CpuMappedLinearBuffer* frame_allocator = &this->frame_allocators.Current();
	frame_allocator->Reset();
	CbvSrvUavStack* descriptor_allocator = &this->resources.cbv_uav_srv_frame_allocators.Current();
	descriptor_allocator->Reset();

	CommandContext command_context;
	command_context.Init(this->graphics_command_list.Get(), descriptor_allocator, frame_allocator, &this->resource_barriers);

	// Set descriptor heaps.
	ID3D12DescriptorHeap* descriptor_heaps[] = {
		this->resources.cbv_uav_srv_allocator.DescriptorHeap(),
		this->resources.sampler_allocator.DescriptorHeap(),
	};
	this->graphics_command_list->SetDescriptorHeaps(std::size(descriptor_heaps), descriptor_heaps);

	// Generate environment map.
	if (environment_map.equirectangular_image.Resource()) {
		command_context.BeginEvent("Environment Map");
		environment_map.CreateEnvironmentMap(&command_context, &environment_map.equirectangular_image, &map);
		deferred_release.Current().push_back(environment_map.equirectangular_image); // TODO: Need a proper mechanism for deferred deletion.
		environment_map.equirectangular_image.Invalidate();
		environment_map_loaded = true;
		command_context.EndEvent();
	}

	GatherLights(gltf, scene, frame_allocator);
	GatherMaterials(gltf, frame_allocator);

	command_context.BeginEvent("Skinning");
	gpu_skinner.Bind(&command_context);
	PerformSkinning(&command_context, gltf, scene);
	command_context.EndEvent();

	if (settings->renderer_type == RENDERER_TYPE_RASTERIZER) {
		Rasterizer::ExecuteParams params = {
			.gltf = gltf,
        	.scene = scene,
        	.camera = camera,
        	.gpu_materials = this->gpu_materials,
        	.gpu_lights = this->gpu_lights,
        	.light_count = (int)this->lights.size(),
        	.environment_map = environment_map_loaded ? &map : nullptr,
        	.output = &this->display,
		};
		rasterizer.DrawScene(&command_context, &settings->raster, &params);
	} else {
		Pathtracer::ExecuteParams params = {
			.gltf = gltf,
        	.scene = scene,
        	.camera = camera,
        	.width = this->display_width,
        	.height = this->display_height,
        	.frame = this->frame,
        	.gpu_materials = this->gpu_materials,
        	.gpu_lights = this->gpu_lights,
        	.light_count = (int)this->lights.size(),
        	.environment_map = environment_map_loaded ? &map : nullptr,
        	.output_descriptor = this->display.Uav(),
        	.output_resource = this->display.Resource(),
		};
		pathtracer.PathtraceScene(&command_context, &settings->pathtracer, &params);
	}

	CD3DX12_RECT scissor_rect(0, 0, this->display_width, this->display_height);
	graphics_command_list->RSSetScissorRects(1, &scissor_rect);
	CD3DX12_VIEWPORT viewport(0.0, 0.0, this->display_width, this->display_height);
	graphics_command_list->RSSetViewports(1, &viewport);
	SetViewportAndScissorRects(this->graphics_command_list.Get(), this->display_width, this->display_height);
	swapchain.TransitionBackbufferForRendering(this->graphics_command_list.Get());
	this->graphics_command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_rtv = swapchain.GetCurrentBackbufferRtv();
	this->graphics_command_list->OMSetRenderTargets(1, &backbuffer_rtv, false, nullptr);

	// Tone mapping.
	command_context.BeginEvent("Tone Mapping");
	this->tone_mapper.Run(&command_context, this->resources.cbv_uav_srv_dynamic_allocator.GetGpuHandle(this->display.Uav()), &this->settings.tone_mapper_config);
	command_context.EndEvent();

	command_context.BeginEvent("ImGui");
	DrawImGui();
	command_context.EndEvent();

	EndFrame();
	ProfilePlotBytes("Transient Allocator", (int64_t)frame_allocator->Size());
	ProfilePlotNumber("Transient Descriptors", (int64_t)descriptor_allocator->Size());
}

void Renderer::LoadLookupTables(UploadBuffer* upload_buffer)
{
	ProfileZoneScoped();
	HRESULT result = S_OK;
	{
		int x, y;
		const char* file = "Sheen_E.exr";
		const char* error;

		EXRVersion exr_version;
		int ret = ParseEXRVersionFromFile(&exr_version, file);
		assert(ret == TINYEXR_SUCCESS);
		assert(exr_version.tiled == 0);
		assert(exr_version.multipart == 0);
		assert(exr_version.non_image == 0);

		EXRHeader exr_header;
		InitEXRHeader(&exr_header);
		ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, file, &error);
		assert(ret == TINYEXR_SUCCESS);
		assert(exr_header.num_channels == 1);
		assert(exr_header.channels[0].pixel_type == TINYEXR_PIXELTYPE_HALF);

		EXRImage exr_image;
		InitEXRImage(&exr_image);

		ret = LoadEXRImageFromFile(&exr_image, &exr_header, file, &error);
		assert(ret == TINYEXR_SUCCESS);
		x = exr_image.width;
		y = exr_image.height;

		Gpu::TextureDesc texture_desc = {
			.format = DXGI_FORMAT_R16_FLOAT,
			.width = (uint16_t)x,
			.height = (uint16_t)y,
			.mip_levels = 1,
			.flags = Gpu::TEXTURE_FLAG_SRV,
			.name = "Sheen E Lookup Table",
		};
		result = resources.CreateTexture(&texture_desc, &this->sheen_e);
		assert(result == S_OK);

		// TODO: Use the SRV created with the texture instead of this. Remove static descriptors altogether.
		D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_handle = resources.cbv_uav_srv_allocator.GetCpuHandle(Gpu::Resources::STATIC_DESCRIPTOR_SRV_SHEEN_E);
		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			.Format = DXGI_FORMAT_R16_FLOAT,
			.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = std::numeric_limits<uint32_t>::max()
			}
		};
		device->CreateShaderResourceView(this->sheen_e.Resource(), &srv_desc, descriptor_cpu_handle);

		int stride = x * 2;
		uint32_t row_pitch = 0;
		std::byte* upload_ptr = (std::byte*)upload_buffer->QueueTextureUpload(DXGI_FORMAT_R16_FLOAT, x, y, 1, this->sheen_e.Resource(), 0, &row_pitch);
		for (int i = 0; i < y; i++) {
			memcpy(upload_ptr + row_pitch * i, exr_image.images[0] + stride * i, stride);
		}

		FreeEXRImage(&exr_image);
		FreeEXRHeader(&exr_header);
	}
}

void Renderer::CreateRenderTargets()
{
	HRESULT result = S_OK;

	Gpu::TextureDesc desc = {
		.format = this->settings.renderer_type == RENDERER_TYPE_PATHTRACER ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT,
		.width = (uint16_t)display_width,
		.height = (uint16_t)display_height,
		.mip_levels = 1,
		.clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
		.flags = (Gpu::TextureFlags)(Gpu::TEXTURE_FLAG_RENDER_TARGET | Gpu::TEXTURE_FLAG_UAV | Gpu::TEXTURE_FLAG_SRV),
		.name = "Display",
	};
	this->resources.CreateTexture(&desc, &this->display);
}

void Renderer::PerformSkinning(CommandContext* context, Gltf* gltf, int scene)
{
	gltf->TraverseScene(scene, [&](Gltf* gltf, int node_id) {
		const Gltf::Node& node = gltf->nodes[node_id];
		bool skinned = node.skin_id != -1;
		bool morphed = node.current_weights.size() > 0;
		if (skinned || morphed) {

			// Calculate and upload bones to gpu.
			D3D12_GPU_VIRTUAL_ADDRESS gpu_bones = 0;
			if (skinned) {
				Gltf::Skin& skin = gltf->skins[node.skin_id];
				GpuSkin::Bone* bones = (GpuSkin::Bone*)context->Allocate(sizeof(bones[0]) * skin.joints.size(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &gpu_bones);
				for (int i = 0; i < skin.joints.size(); i++) {
					int joint = skin.joints[i];
					bones[i].transform = glm::affineInverse(node.global_transform) * gltf->nodes[joint].global_transform * skin.inverse_bind_poses[i];
					bones[i].inverse_transpose = glm::inverseTranspose(glm::mat3x3(bones[i].transform));
				}
			}

			// Perform gpu skinning.
			std::vector<Gltf::Primitive>& primitive = gltf->meshes[node.mesh_id].primitives;
			std::vector<DynamicMesh>& dynamic = gltf->dynamic_primitives[node.dynamic_mesh].dynamic_meshes;
			for (int i = 0; i < primitive.size(); i++) {
				dynamic[i].Flip();

				// Pick only the largest weights, and ignore any weights that are not larger than 0.
				int num_of_targets = 0;
				float weights[::Config::MAX_SIMULTANEOUS_MORPH_TARGETS] = {};
				MorphTarget* targets[::Config::MAX_SIMULTANEOUS_MORPH_TARGETS] = {};
				for (int j = 0; j < node.current_weights.size(); j++) {
					if (node.current_weights[j] > 0.0f) {
						if (num_of_targets < ::Config::MAX_SIMULTANEOUS_MORPH_TARGETS) {
							weights[num_of_targets] = node.current_weights[j];
							targets[num_of_targets] = &primitive[i].targets[j];
							num_of_targets++;
						} else {
							int min_index = std::distance(&weights[0], std::min_element(&weights[0], &weights[::Config::MAX_SIMULTANEOUS_MORPH_TARGETS]));
							if (weights[min_index] < node.current_weights[j]) {
								weights[min_index] = node.current_weights[j];
								targets[min_index] = &primitive[i].targets[j];
							}
						}
					}
				}

				gpu_skinner.Run(
					context,
					&primitive[i].mesh,
					&dynamic[i],
					skinned ? gpu_bones : 0,
					num_of_targets,
					targets,
					weights
				);
			}
		}
	});
}

void Renderer::GatherLights(Gltf* gltf, int scene, CpuMappedLinearBuffer* allocator)
{
	lights.clear();
	gltf->TraverseScene(scene, [&](Gltf* gltf, int node_id) {
		const Gltf::Node& node = gltf->nodes[node_id];
		int light_id = node.light_id;
		if (light_id != -1) {
			const Gltf::Light& scene_light = gltf->lights[light_id];
			GpuLight light;
			switch (scene_light.type) {
				case Gltf::Light::TYPE_POINT:
					light.type = GpuLight::TYPE_POINT;
					break;
				case Gltf::Light::TYPE_SPOT:
					light.type = GpuLight::TYPE_SPOT;
					break;
				case Gltf::Light::TYPE_DIRECTIONAL:
					light.type = GpuLight::TYPE_DIRECTIONAL;
					break;
			}
			light.color = scene_light.color;
			light.intensity = scene_light.intensity;
			light.cutoff = scene_light.cutoff;
			light.position = node.global_transform[3];
			light.direction = glm::normalize(glm::inverseTranspose(node.global_transform) * glm::vec4(0.0, 0.0, -1.0, 0.0));
			light.inner_angle = scene_light.inner_angle;
			light.outer_angle = scene_light.outer_angle;
			lights.emplace_back(light);
		}
	});
	if (lights.data()) {
		this->gpu_lights = allocator->Copy(lights.data(), sizeof(GpuLight) * lights.size(), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	}
}

void Renderer::GatherMaterials(Gltf* gltf, CpuMappedLinearBuffer* allocator)
{
	GpuMaterial* gpu_materials = (GpuMaterial*)allocator->Allocate(sizeof(GpuMaterial) * (gltf->materials.size()), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, &this->gpu_materials);
	for (int i = 0; i < gltf->materials.size(); i++) {
		gpu_materials[i] = GpuMaterial(gltf->materials[i]);
	}
}

void Renderer::EndFrame()
{
	// Submit commands to gpu.
	swapchain.TransitionBackbufferForPresenting(this->graphics_command_list.Get());
	HRESULT result = this->graphics_command_list->Close();
	assert(result == S_OK);
	auto command_list = this->graphics_command_list.Get();
	this->graphics_command_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&command_list);
	swapchain.Present(this->graphics_command_queue.Get(), this->settings.vsync_interval);

	// Fire a signal when frame is rendered.
	result = this->graphics_command_queue->Signal(fence.Get(), current_frame);
	assert(result == S_OK);

	fence_values.Current() = current_frame;
	frame++;
	fence_values.Next();
	frame_allocators.Next();
	graphics_command_allocators.Next();
	resources.cbv_uav_srv_frame_allocators.Next();
}

void Renderer::WaitForOutstandingWork()
{
	ProfileZoneScoped();
	// Wait for GPU to finish rendering queued frames.
	uint64_t completed_frame = this->fence->GetCompletedValue();
	if (completed_frame < this->current_frame) {
		HRESULT result = this->fence->SetEventOnCompletion(this->current_frame, this->frame_event);
		assert(result == S_OK);
		WaitForSingleObjectEx(this->frame_event, INFINITE, FALSE);
	}
}

void Renderer::Destroy()
{
	ImGui_ImplDX12_Shutdown();
}

void Renderer::SetViewportAndScissorRects(ID3D12GraphicsCommandList* command_list, int width, int height)
{
	CD3DX12_VIEWPORT viewport(0.0f, 0.0f, width, height);
	command_list->RSSetViewports(1, &viewport);
	CD3DX12_RECT scissor_rect(0, 0, width, height);
	command_list->RSSetScissorRects(1, &scissor_rect);
}