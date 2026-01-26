#include "Renderer.h"

#include <algorithm>

#include <directx/d3d12.h>
#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/dxgiformat.h>
#include <imgui/backends/imgui_impl_dx12.h>
#include <spdlog/spdlog.h>

#include "Config.h"
#include "DescriptorAllocator.h"
#include "ForwardPass.h"
#include "GpuResources.h"
#include "GpuSkin.h"
#include "BufferAllocator.h"

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
	result = graphics_command_queue->SetName(L"Graphics Command Queue");
	assert(result == S_OK);

	// Create command allocators.
	for (int i = 0; i < ::Config::FRAME_COUNT; i++) {
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(graphics_command_allocators[i].ReleaseAndGetAddressOf()));
		assert(result == S_OK);
		if (result != S_OK) {
			SPDLOG_ERROR("Failed to create command allocator.");
			return false;
		}
		result = graphics_command_allocators[i]->SetName(L"Graphics Command Allocator");
		assert(result == S_OK);
	}

	// Create the command list.
	result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, graphics_command_allocators.Current().Get(), nullptr, IID_PPV_ARGS(graphics_command_list.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	if (result != S_OK) {
		SPDLOG_ERROR("Failed to create command list.");
		return false;
	}
	result = graphics_command_list->SetName(L"Graphics Command List");
	assert(result == S_OK);

	// Create frame fence.
	result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(this->fence.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = this->fence->SetName(L"Frame Fence");
	assert(result == S_OK);
	this->frame_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	// Create the swapchain.
	swapchain.Create(this->device.Get(), this->graphics_command_queue.Get(), &this->resources, window, display_width, display_height);
	
	upload_buffer.Create(this->device.Get(), ::Config::UPLOAD_BUFFER_CAPACITY, D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, ::Config::FRAME_COUNT);

	for (int i = 0; i < frame_allocators.Size(); i++) {
		frame_allocators[i].Create(this->device.Get(), ::Config::FRAME_HEAP_CAPACITY, true, L"Transient Resources");
	}

	acceleration_structure.Init(this->device.Get(), Config::MAX_BLAS_VERTICES, Config::MAX_TLAS_INSTANCES);

	InitializeImGui();

	upload_buffer.Begin();

	CreateRenderTargets();
	gpu_skinner.Create(this->device.Get());
	tone_mapper.Create(this->device.Get(), &this->resources);
	environment_map.Init(this->device.Get(), &this->resources.cbv_uav_srv_dynamic_allocator);
	resources.LoadLookupTables(&this->upload_buffer);

	if (settings->renderer_type == RENDERER_TYPE_RASTERIZER) {
		forward.Create(this->device.Get());
	} else {
		pathtracer.Create(this->device.Get(), &this->upload_buffer);
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
		DescriptorPool* descriptor_pool = (DescriptorPool*)info->UserData;
		int descriptor = descriptor_pool->Allocate();
		*out_cpu_desc_handle = descriptor_pool->GetCpuHandle(descriptor);
		*out_gpu_desc_handle = descriptor_pool->GetGpuHandle(descriptor);
	};
    imgui.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle) {
		DescriptorPool* descriptor_pool = (DescriptorPool*)info->UserData;
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

void Renderer::CreateRendererTypeSpecificResources(RendererType renderer_type)
{
	if (renderer_type == RENDERER_TYPE_RASTERIZER) {
		forward.Create(this->device.Get());
	} else {
		pathtracer.Create(this->device.Get(), &this->upload_buffer);
	}
}

void Renderer::DestroyRendererTypeSpecificResources(RendererType renderer_type)
{
	if (renderer_type == RENDERER_TYPE_RASTERIZER) {
		forward.Destroy();
	} else {
		pathtracer.Destroy();
	}
}

void Renderer::ApplySettingsChanges(const Renderer::RenderSettings* new_settings)
{
	bool recreate_render_targets = false;

	// Change renderer.
	if (new_settings->renderer_type != this->settings.renderer_type) {
		WaitForOutstandingWork();
		this->upload_buffer.Begin();
		DestroyRendererTypeSpecificResources(this->settings.renderer_type);
		CreateRendererTypeSpecificResources(new_settings->renderer_type);
		uint64_t submission = this->upload_buffer.Submit();
		this->upload_buffer.WaitForSubmissionToComplete(submission);
		recreate_render_targets = true;
	}

	// Set display resolution.
	if (new_settings->width != this->settings.width || new_settings->height != this->settings.height) {
		WaitForOutstandingWork();
		this->display_width = new_settings->width;
		this->display_height = new_settings->height;
		this->swapchain.Resize(this->device.Get(), &this->resources, this->display_width, this->display_height);
		recreate_render_targets = true;
	}
		
	this->settings = *new_settings;
	
	// Set display settings.
	if (recreate_render_targets) {
		CreateRenderTargets();
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
	DescriptorStack* descriptor_allocator = &this->resources.cbv_uav_srv_frame_allocators.Current();
	descriptor_allocator->Reset();

	// Set descriptor heaps.
	ID3D12DescriptorHeap* descriptor_heaps[] = {
		this->resources.cbv_uav_srv_allocator.DescriptorHeap(),
		this->resources.sampler_allocator.DescriptorHeap(),
	};
	this->graphics_command_list->SetDescriptorHeaps(std::size(descriptor_heaps), descriptor_heaps);

	// Generate environment map.
	if (environment_map.equirectangular_image.Get()) {
		environment_map.CreateEnvironmentMap(this->graphics_command_list.Get(), frame_allocator, descriptor_allocator, environment_map.equirectangular_image.Get(), &map);
		deferred_release.Current().push_back(environment_map.equirectangular_image);
		environment_map.equirectangular_image.Reset();
		environment_map_loaded = true;
	}

	GatherLights(gltf, scene, frame_allocator);
	GatherMaterials(gltf, frame_allocator);

	gpu_skinner.Bind(this->graphics_command_list.Get());
	PerformSkinning(gltf, scene, frame_allocator);

	if (settings->renderer_type == RENDERER_TYPE_RASTERIZER) {
		RasterizeScene(this->graphics_command_list.Get(), frame_allocator, descriptor_allocator, gltf, scene, camera, settings);
	} else {
		PathtraceScene(this->graphics_command_list.Get(), frame_allocator, descriptor_allocator, gltf, scene, camera, settings);
	}
	
	CD3DX12_RECT scissor_rect(0, 0, this->display_width, this->display_height);
	graphics_command_list->RSSetScissorRects(1, &scissor_rect);
	CD3DX12_VIEWPORT viewport(0.0, 0.0, this->display_width, this->display_height);
	graphics_command_list->RSSetViewports(1, &viewport);
	SetViewportAndScissorRects(this->graphics_command_list.Get(), this->display_width, this->display_height);
	swapchain.TransitionBackbufferForRendering(this->graphics_command_list.Get());
	D3D12_CPU_DESCRIPTOR_HANDLE backbuffer_rtv = resources.rtv_allocator.GetCpuHandle(GpuResources::RTV_SWAPCHAIN_0 + swapchain.GetCurrentBackbufferIndex());
	this->graphics_command_list->OMSetRenderTargets(1, &backbuffer_rtv, false, nullptr);

	// Tone mapping.
	this->tone_mapper.Run(this->graphics_command_list.Get(), frame_allocator, this->resources.cbv_uav_srv_allocator.GetGpuHandle(GpuResources::STATIC_DESCRIPTOR_UAV_DISPLAY), &this->settings.tone_mapper_config);
	DrawImGui();

	EndFrame();
}

void Renderer::SortRenderObjects(glm::vec3 camera_pos)
{
	auto comparison = [&](const RenderObject& a, const RenderObject& b) -> bool {
		glm::vec3 pos_a = glm::vec3(a.transform[3]) - camera_pos;
		glm::vec3 pos_b = glm::vec3(b.transform[3]) - camera_pos;
		return (glm::dot(pos_a, pos_a)) > (glm::dot(pos_b, pos_b));
	};
	std::sort(alpha_render_objects.begin(), alpha_render_objects.end(), comparison);
	std::sort(transparent_render_objects.begin(), transparent_render_objects.end(), comparison);
}

void Renderer::CreateRenderTargets()
{
    CD3DX12_HEAP_PROPERTIES render_target_heap_properties(D3D12_HEAP_TYPE_DEFAULT);

	HRESULT result;
	if (this->settings.renderer_type == RENDERER_TYPE_RASTERIZER) {
		// Depth buffer.
		{
			CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, display_width, display_height, 1, 1);
			resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
			
			CD3DX12_CLEAR_VALUE clear_value(DXGI_FORMAT_D32_FLOAT, DEPTH_CLEAR_VALUE, 0);
			result = device->CreateCommittedResource(&render_target_heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear_value, IID_PPV_ARGS(this->depth.ReleaseAndGetAddressOf()));
			assert(result == S_OK);
			result = depth->SetName(L"Depth Texture");
			assert(result == S_OK);

			resources.dsv_allocator.CreateDsv(GpuResources::DSV_DEPTH, this->depth.Get(), nullptr);

			CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT);
			resources.cbv_uav_srv_allocator.CreateSrv(GpuResources::STATIC_DESCRIPTOR_SRV_DEPTH, this->depth.Get(), &srv_desc);
		}

		// Motion vectors.
		{
			CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, display_width, display_height, 1, 1);
			resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

			float clear_color[4] = {0.0, 0.0, 0.0, 0.0};
			CD3DX12_CLEAR_VALUE clear_value(DXGI_FORMAT_R16G16_FLOAT, clear_color);

			result = device->CreateCommittedResource(&render_target_heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear_value, IID_PPV_ARGS(this->motion_vectors.ReleaseAndGetAddressOf()));
			assert(result == S_OK);
			result = this->motion_vectors->SetName(L"Motion Vectors");
			assert(result == S_OK);

			resources.rtv_allocator.CreateRtv(GpuResources::RTV_MOTION_VECTORS, this->motion_vectors.Get(), nullptr);
			resources.cbv_uav_srv_allocator.CreateSrv(GpuResources::STATIC_DESCRIPTOR_SRV_MOTION_VECTORS, this->motion_vectors.Get(), nullptr);
		}

		// Transmission.
		{
			CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, display_width, display_height, 1);
			resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
						
			result = device->CreateCommittedResource(&render_target_heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(this->transmission.ReleaseAndGetAddressOf()));
			assert(result == S_OK);
			result = this->transmission->SetName(L"Transmission");
			assert(result == S_OK);

			resources.cbv_uav_srv_allocator.CreateSrv(GpuResources::STATIC_DESCRIPTOR_SRV_TRANSMISSION, this->transmission.Get(), nullptr);
		}
		
	}

	// Display.
	{
		DXGI_FORMAT display_format = this->settings.renderer_type == RENDERER_TYPE_PATHTRACER ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R16G16B16A16_FLOAT;
		CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(display_format, display_width, display_height, 1, 1);
		resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		
		float clear_color[4] = {0.0, 0.0, 0.0, 0.0};
		CD3DX12_CLEAR_VALUE clear_value(display_format, clear_color);
		
		result = device->CreateCommittedResource(&render_target_heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear_value, IID_PPV_ARGS(this->display.ReleaseAndGetAddressOf()));
		assert(result == S_OK);
		result = this->display->SetName(L"Display");
		assert(result == S_OK);

		resources.rtv_allocator.CreateRtv(GpuResources::RTV_DISPLAY, this->display.Get(), nullptr);
		resources.cbv_uav_srv_allocator.CreateUav(GpuResources::STATIC_DESCRIPTOR_UAV_DISPLAY, this->display.Get(), nullptr, nullptr);
	}
}

void Renderer::PerformSkinning(Gltf* gltf, int scene, CpuMappedLinearBuffer* frame_allocator)
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
				GpuSkin::Bone* bones = (GpuSkin::Bone*)frame_allocator->Allocate(sizeof(bones[0]) * skin.joints.size(), D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, &gpu_bones);
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
					this->graphics_command_list.Get(),
					frame_allocator,
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

void Renderer::GatherRenderObjects(Gltf* gltf, int scene)
{
	opaque_render_objects.clear();
	alpha_mask_render_objects.clear();
	alpha_render_objects.clear();
	transparent_render_objects.clear();

	gltf->TraverseScene(scene, [&](Gltf* gltf, int node_id) {
		const Gltf::Node& node = gltf->nodes[node_id];
		if (node.mesh_id != -1) {
			const Gltf::Mesh& mesh = gltf->meshes[node.mesh_id];
			for (int i = 0; i < mesh.primitives.size(); i++) {

				// Gather the data needed to render an object.
				int material_id = mesh.primitives[i].material_id;
				RenderObject render_object = {
					.transform = node.global_transform,
					.normal_transform = glm::inverseTranspose(glm::mat3x3(node.global_transform)),
					.previous_transform = node.previous_global_transform,
					.mesh_id = node.mesh_id,
					.dynamic_mesh_id = node.dynamic_mesh,
					.primitive_id = i,
					.material_id = material_id,
				};

				// Bin the render object depending on material properties.
				const Gltf::Material& material = gltf->materials[material_id];
				if (material.alpha_mode == Gltf::Material::ALPHA_MODE_BLEND) {
					alpha_render_objects.push_back(render_object);
				} else if (material.alpha_mode == Gltf::Material::ALPHA_MODE_MASK) {
					alpha_mask_render_objects.push_back(render_object);
				} else if (material.transmission_factor > 0.0f) {
					transparent_render_objects.push_back(render_object);
				} else {
					opaque_render_objects.push_back(render_object);
				}
			}
		}
	});
}

void Renderer::DrawRenderObjects(Gltf* gltf, CpuMappedLinearBuffer* frame_allocator, const std::vector<RenderObject>& render_objects)
{
	for (auto& render_object: render_objects) {
		DynamicMesh* dynamic_mesh = render_object.dynamic_mesh_id != -1 ? &gltf->dynamic_primitives[render_object.dynamic_mesh_id].dynamic_meshes[render_object.primitive_id] : nullptr;
		forward.Draw(
			this->graphics_command_list.Get(),
			frame_allocator,
			&gltf->meshes[render_object.mesh_id].primitives[render_object.primitive_id].mesh,
			render_object.material_id,
			render_object.transform,
			render_object.normal_transform,
			render_object.previous_transform,
			dynamic_mesh
		);
	}
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

void Renderer::BuildAllBlas(Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list)
{
    for (int i = 0; i < gltf->nodes.size(); i++) {
		Gltf::Node& node = gltf->nodes[i];
		int mesh_id = node.mesh_id;
        if (mesh_id != -1) {
            Gltf::Mesh& mesh = gltf->meshes[mesh_id];
			for (int j = 0; j < mesh.primitives.size(); j++) {
				Gltf::Primitive& primitive = mesh.primitives[j];
				int dynamic_meshes_id = node.dynamic_mesh;
				if (dynamic_meshes_id != -1) {
					// Dynamic.
					DynamicMesh& dynamic_mesh = gltf->dynamic_primitives[dynamic_meshes_id].dynamic_meshes[j];
					gltf->dynamic_primitives[dynamic_meshes_id].dynamic_blases.resize(gltf->dynamic_primitives[dynamic_meshes_id].dynamic_meshes.size());
					RaytracingAccelerationStructure::DynamicBlas& dynamic_blas = gltf->dynamic_primitives[dynamic_meshes_id].dynamic_blases[j];
					if (!dynamic_blas.resource.Get()) {
						acceleration_structure->BuildDynamicBlas(command_list, primitive.mesh.position.view.BufferLocation, primitive.mesh.num_of_vertices, primitive.mesh.index.view, primitive.mesh.num_of_indices, &dynamic_blas);
					}
				} else {
					// Static.
					if (!primitive.blas.resource.Get()) {
						acceleration_structure->BuildStaticBlas(command_list, primitive.mesh.position.view.BufferLocation, primitive.mesh.num_of_vertices, primitive.mesh.index.view, primitive.mesh.num_of_indices, &primitive.blas);
					}
				}
			}
        }
    }
    acceleration_structure->EndBlasBuilds(command_list);
}

void Renderer::UpdateAllBlas(Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list)
{
    for (int i = 0; i < gltf->nodes.size(); i++) {
		Gltf::Node& node = gltf->nodes[i];
		int mesh_id = node.mesh_id;
		int skin_id = node.dynamic_mesh;
        if (mesh_id != -1 && skin_id != -1) {
			std::vector<Gltf::Primitive>& primitives = gltf->meshes[mesh_id].primitives; 
			Gltf::DynamicPrimitives& dynamic_primitives = gltf->dynamic_primitives[skin_id];
			for (int j = 0; j < dynamic_primitives.dynamic_blases.size(); j++) {
				acceleration_structure->UpdateDynamicBlas(command_list, &dynamic_primitives.dynamic_blases[j], dynamic_primitives.dynamic_meshes[j].GetCurrentPositionBuffer()->view.BufferLocation, primitives[j].mesh.num_of_vertices, primitives[j].mesh.index.view, primitives[j].mesh.num_of_indices);
			}
        }
    }
    acceleration_structure->EndBlasBuilds(command_list);
}

void Renderer::BuildTlas(Gltf* gltf, int scene_id, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list, CpuMappedLinearBuffer* allocator)
{
	mesh_instances.clear();
    acceleration_structure->BeginTlasBuild();

	// TODO: Define this somewhere else?
	enum InstanceMask {
		MASK_NONE = 1 << 0,
		MASK_ALPHA_BLEND = 1 << 1,
	};
	gltf->TraverseScene(scene_id, [&](Gltf* gltf, int node_id) {
		const Gltf::Node& node = gltf->nodes[node_id];
		int mesh_id = node.mesh_id;
		if (mesh_id != -1) {
			std::vector<Gltf::Primitive>& primitives = gltf->meshes[mesh_id].primitives; 
			for (int i = 0; i < primitives.size(); i++) {
				const Mesh& mesh = primitives[i].mesh;
				const Gltf::Material& material = gltf->materials[primitives[i].material_id];
				GpuMeshInstance gpu_mesh_instance = {
					.transform = node.global_transform,
					.normal_transform = glm::inverseTranspose(node.global_transform),
					.index_descriptor = mesh.index.descriptor,
					.position_descriptor = mesh.position.descriptor,
					.normal_descriptor = mesh.normal.descriptor,
					.tangent_descriptor = mesh.tangent.descriptor,
					.texcoord_descriptors = {
						mesh.texcoords[0].descriptor,
						mesh.texcoords[1].descriptor,
					},
					.color_descriptor = mesh.color.descriptor,
					.material_id = primitives[i].material_id,
				};
				unsigned int flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
				if (material.flags & Gltf::Material::FLAG_DOUBLE_SIDED) {
					flags |=  D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
				}
				if (material.alpha_mode == Gltf::Material::ALPHA_MODE_MASK) {
					flags |=  D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
				}
				unsigned int instance_mask = 0;
				if (material.alpha_mode == Gltf::Material::ALPHA_MODE_BLEND) {
					instance_mask = MASK_ALPHA_BLEND;
				} else {
					instance_mask = MASK_NONE;
				}
				bool tlas_added = false;
				if (gltf->nodes[node_id].dynamic_mesh != -1) {
					if (gltf->dynamic_primitives[node.dynamic_mesh].dynamic_blases.size() > i) {
						// Dynamic.
						DynamicMesh& dynamic_mesh = gltf->dynamic_primitives[node.dynamic_mesh].dynamic_meshes[i];
						RaytracingAccelerationStructure::DynamicBlas& dynamic_blas = gltf->dynamic_primitives[node.dynamic_mesh].dynamic_blases[i];
						tlas_added = acceleration_structure->AddTlasInstance(&dynamic_blas, node.global_transform, instance_mask, flags);
						if (dynamic_mesh.flags & DynamicMesh::Flags::FLAG_POSITION) {
							gpu_mesh_instance.position_descriptor = dynamic_mesh.GetCurrentPositionBuffer()->descriptor;
						}
						if (dynamic_mesh.flags & DynamicMesh::Flags::FLAG_NORMAL) {
							gpu_mesh_instance.normal_descriptor = dynamic_mesh.normal.descriptor;
						}
						if (dynamic_mesh.flags & DynamicMesh::Flags::FLAG_TANGENT) {
							gpu_mesh_instance.tangent_descriptor = dynamic_mesh.tangent.descriptor;
						}
					}
				} else {
					// Static.
					RaytracingAccelerationStructure::Blas& blas = primitives[i].blas;
					tlas_added = acceleration_structure->AddTlasInstance(&blas, node.global_transform, instance_mask, flags);
				}
				if (tlas_added) {
					mesh_instances.push_back(gpu_mesh_instance);
				}
			}
		}
	});

    acceleration_structure->BuildTlas(command_list);
	this->gpu_mesh_instances = allocator->Copy(mesh_instances.data(), sizeof(GpuMeshInstance) * mesh_instances.size(), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
}

void Renderer::RasterizeScene(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* frame_allocator, DescriptorStack* descriptor_allocator, Gltf* gltf, int scene, Camera* camera, const RenderSettings* settings)
{
	GatherRenderObjects(gltf, scene);

	// Get transform matrices.
	glm::mat4x4 world_to_view = camera->GetWorldToView();
	glm::mat4x4 world_to_clip = camera->GetViewToClip() * world_to_view;
	glm::mat4x4 view_to_world = glm::affineInverse(world_to_view);
	glm::mat4x4 clip_to_world = glm::inverse(world_to_clip);
	glm::vec3 camera_pos = view_to_world[3];

	SortRenderObjects(camera_pos);

	// Prepare render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE render_rtv = resources.rtv_allocator.GetCpuHandle(GpuResources::RTV_DISPLAY); 
	D3D12_CPU_DESCRIPTOR_HANDLE motion_vectors_rtv = resources.rtv_allocator.GetCpuHandle(GpuResources::RTV_MOTION_VECTORS); 
	D3D12_CPU_DESCRIPTOR_HANDLE null_rtv = {}; 

	CD3DX12_RESOURCE_BARRIER barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			display.Get(), 
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
			D3D12_RESOURCE_STATE_RENDER_TARGET
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			motion_vectors.Get(), 
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
			D3D12_RESOURCE_STATE_RENDER_TARGET
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			depth.Get(), 
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		),
	};
	command_list->ResourceBarrier(std::size(barriers), barriers);
	
	float clear_color[4] = {0., 0., 0., 0.};
	command_list->ClearRenderTargetView(render_rtv, clear_color, 0, nullptr);
	command_list->ClearRenderTargetView(motion_vectors_rtv, clear_color, 0, nullptr);
	command_list->ClearDepthStencilView(resources.dsv_allocator.GetCpuHandle(GpuResources::DSV_DEPTH), D3D12_CLEAR_FLAG_DEPTH, DEPTH_CLEAR_VALUE, 0, 0, nullptr);
	
	SetViewportAndScissorRects(command_list, this->display_width, this->display_height);

	// Render opaque objects.
	ForwardPass::Config config = {
		.width = (int)this->display_width,
		.height = (int)this->display_height,
		.world_to_clip = world_to_clip,
		.previous_world_to_clip = this->previous_world_to_clip,
		.camera_pos = camera_pos,
		.num_of_lights = (int)this->lights.size(),
		.lights = this->gpu_lights,
		.materials = this->gpu_materials,
		.ggx_cube_descriptor = environment_map_loaded ? map.ggx_srv_descriptor : -1,
		.diffuse_cube_descriptor = environment_map_loaded ? map.diffuse_srv_descriptor : -1,
		.environment_map_intensity = 1.0,
		.transmission_descriptor = -1,
		.render_flags = settings->raster.render_flags,
	};
	D3D12_PRIMITIVE_TOPOLOGY primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	this->graphics_command_list->IASetPrimitiveTopology(primitive_topology);
	forward.SetRootSignature(command_list);
	forward.SetConfig(command_list, frame_allocator, &config);
	forward.BindRenderTargets(command_list, &this->resources, render_rtv, motion_vectors_rtv);
	forward.BindPipeline(command_list, &this->resources, ForwardPass::PIPELINE_FLAGS_NONE);
	DrawRenderObjects(gltf, frame_allocator, opaque_render_objects);

	// TODO: Create a separate pipeline for alpha mask instead of sharing the opaque pass. This could potentially improve performance of the opaque rendering.
	DrawRenderObjects(gltf, frame_allocator, alpha_mask_render_objects);

	if (environment_map_loaded) {
		forward.DrawBackground(command_list, frame_allocator, clip_to_world, 1.0, map.cube_srv_descriptor);
		
		// Set pipeline state back to rendering meshes.
		forward.SetRootSignature(command_list);
		forward.SetConfig(command_list, frame_allocator, &config);
	}

	// Create transmission mip chain.
	{
		CD3DX12_RESOURCE_BARRIER resource_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			display.Get(), 
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_COPY_SOURCE
		);
		command_list->ResourceBarrier(1, &resource_barrier);
	}
	forward.GenerateTransmissionMips(command_list, frame_allocator, descriptor_allocator, this->display.Get(), this->transmission.Get());
	{
		CD3DX12_RESOURCE_BARRIER resource_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			display.Get(), 
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		);
		command_list->ResourceBarrier(1, &resource_barrier);
	}

	config.transmission_descriptor = GpuResources::STATIC_DESCRIPTOR_SRV_TRANSMISSION;
	forward.SetConfig(command_list, frame_allocator, &config);

	// Render transmissives.
	forward.BindPipeline(command_list, &this->resources, ForwardPass::PIPELINE_FLAGS_ALPHA_BLEND);
	DrawRenderObjects(gltf, frame_allocator, transparent_render_objects);

	// Render alpha blended geometry.
	DrawRenderObjects(gltf, frame_allocator, alpha_render_objects);

	// Transition render targets to read state for post processing.
	CD3DX12_RESOURCE_BARRIER resource_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			display.Get(), 
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			motion_vectors.Get(), 
			D3D12_RESOURCE_STATE_RENDER_TARGET, 
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			depth.Get(), 
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		)
	};
	command_list->ResourceBarrier(std::size(resource_barriers), resource_barriers);

	this->previous_world_to_clip = world_to_clip;
}

void Renderer::PathtraceScene(ID3D12GraphicsCommandList4* command_list, CpuMappedLinearBuffer* frame_allocator, DescriptorStack* descriptor_allocator, Gltf* gltf, int scene, Camera* camera, RenderSettings* settings)
{
	glm::mat4x4 world_to_view = camera->GetWorldToView();
	glm::mat4x4 world_to_clip = camera->GetViewToClip() * world_to_view;
	glm::mat4x4 view_to_world = glm::affineInverse(world_to_view);
	glm::mat4x4 clip_to_world = glm::inverse(world_to_clip);
	glm::vec3 camera_pos = view_to_world[3];

	// Reset accumulation if the camera position has changed.
	if (world_to_clip != previous_world_to_clip) {
		settings->pathtracer.reset = true;
	}

	if (pathtracer.AccumulatedFrames() < settings->pathtracer.max_accumulated_frames || settings->pathtracer.reset) {
		command_list->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); // TODO: Why is this here? Is it neccessary?
		
		// Update the acceleration structure.
		BuildAllBlas(gltf, &this->acceleration_structure, command_list);
		UpdateAllBlas(gltf, &this->acceleration_structure, command_list);
		BuildTlas(gltf, scene, &this->acceleration_structure, command_list, frame_allocator);

		// Pathtrace the scene.
		Pathtracer::Parameters parameters = {
			.width = (int)this->display_width,
			.height = (int)this->display_height,
			.acceleration_structure = acceleration_structure.GetAccelerationStructure(),
			.instances = this->gpu_mesh_instances,
			.materials = this->gpu_materials,
			.num_of_lights = (int)this->lights.size(),
			.lights = this->gpu_lights,
			.camera_pos = camera_pos,
			.clip_to_world = clip_to_world,
			.reset_history = settings->pathtracer.reset,
			.min_bounces = settings->pathtracer.min_bounces,
			.max_bounces = settings->pathtracer.max_bounces,
			.debug_output = settings->pathtracer.debug_output,
			.flags = settings->pathtracer.flags,
			.output_descriptor = GpuResources::STATIC_DESCRIPTOR_UAV_DISPLAY,
			.environment_color = settings->pathtracer.environment_color,
			.environment_intensity = settings->pathtracer.environment_intensity,
			.seed = settings->pathtracer.use_frame_as_seed ? (uint32_t)this->frame : settings->pathtracer.seed,
			.environment_cube_map = map.cube_srv_descriptor,
			.environment_importance_map = map.importance_srv_descriptor,
			.luminance_clamp = settings->pathtracer.luminance_clamp,
			.min_russian_roulette_continue_prob = settings->pathtracer.min_russian_roulette_continue_prob,
			.max_russian_roulette_continue_prob = settings->pathtracer.max_russian_roulette_continue_prob,
		};
		pathtracer.Run(command_list, frame_allocator, &parameters);
		
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(this->display.Get());
		command_list->ResourceBarrier(1, &barrier);
	}
	
	previous_world_to_clip = world_to_clip;
}