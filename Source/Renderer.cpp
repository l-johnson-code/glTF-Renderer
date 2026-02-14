#include "Renderer.h"

#include <algorithm>

#include <directx/d3d12.h>
#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/dxgiformat.h>
#include <imgui/backends/imgui_impl_dx12.h>
#include <spdlog/spdlog.h>

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
	
	upload_buffer.Create(this->device.Get(), ::Config::UPLOAD_BUFFER_CAPACITY, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, ::Config::FRAME_COUNT);

	for (int i = 0; i < frame_allocators.Size(); i++) {
		frame_allocators[i].Create(this->device.Get(), ::Config::FRAME_HEAP_CAPACITY, true, "Transient Resources");
	}

	InitializeImGui();

	upload_buffer.Begin();

	CreateRenderTargets();
	gpu_skinner.Create(this->device.Get());
	tone_mapper.Create(this->device.Get());
	environment_map.Init(this->device.Get(), &this->resources.cbv_uav_srv_dynamic_allocator);
	resources.LoadLookupTables(&this->upload_buffer);

	if (settings->renderer_type == RENDERER_TYPE_RASTERIZER) {
		rasterizer.Init(this->device.Get(), &resources.rtv_allocator, &resources.dsv_allocator, &resources.cbv_uav_srv_dynamic_allocator, this->display_width, this->display_height);
	} else {
		pathtracer.Init(this->device.Get(), &this->upload_buffer);
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
		CbvSrvUavPool* descriptor_pool = (CbvSrvUavPool*)info->UserData;
		int descriptor = descriptor_pool->Allocate();
		*out_cpu_desc_handle = descriptor_pool->GetCpuHandle(descriptor);
		*out_gpu_desc_handle = descriptor_pool->GetGpuHandle(descriptor);
	};
    imgui.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle) {
		CbvSrvUavPool* descriptor_pool = (CbvSrvUavPool*)info->UserData;
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
			rasterizer.Init(this->device.Get(), &resources.rtv_allocator, &resources.dsv_allocator, &resources.cbv_uav_srv_dynamic_allocator, new_settings->width, new_settings->height);
		} else {
			this->upload_buffer.Begin();
			pathtracer.Init(this->device.Get(), &this->upload_buffer);
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
	if (environment_map.equirectangular_image.Get()) {
		command_context.BeginEvent("Environment Map");
		environment_map.CreateEnvironmentMap(&command_context, environment_map.equirectangular_image.Get(), &map);
		deferred_release.Current().push_back(environment_map.equirectangular_image);
		environment_map.equirectangular_image.Reset();
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
        	.output_rtv= this->display_rtv,
        	.output_resource = this->display.Get(),
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
        	.output_descriptor = this->display_uav,
        	.output_resource = this->display.Get(),
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
	this->tone_mapper.Run(&command_context, this->resources.cbv_uav_srv_dynamic_allocator.GetGpuHandle(this->display_uav), &this->settings.tone_mapper_config);
	command_context.EndEvent();

	command_context.BeginEvent("ImGui");
	DrawImGui();
	command_context.EndEvent();

	EndFrame();
	ProfilePlotBytes("Transient Allocator", (int64_t)frame_allocator->Size());
	ProfilePlotNumber("Transient Descriptors", (int64_t)descriptor_allocator->Size());
}

void Renderer::CreateRenderTargets()
{
    CD3DX12_HEAP_PROPERTIES render_target_heap_properties(D3D12_HEAP_TYPE_DEFAULT);

	HRESULT result;

	// Display.
	{
		DXGI_FORMAT display_format = this->settings.renderer_type == RENDERER_TYPE_PATHTRACER ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
		CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(display_format, display_width, display_height, 1, 1);
		resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		
		float clear_color[4] = {0.0, 0.0, 0.0, 0.0};
		CD3DX12_CLEAR_VALUE clear_value(display_format, clear_color);
		
		result = GpuResources::CreateCommittedResource(device.Get(), &render_target_heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear_value, this->display.ReleaseAndGetAddressOf(), "Display");
		assert(result == S_OK);

		this->display_rtv = resources.rtv_allocator.AllocateAndCreateRtv(this->display.Get(), nullptr);
		this->display_uav = resources.cbv_uav_srv_dynamic_allocator.AllocateAndCreateUav(this->display.Get(), nullptr, nullptr);
	}
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