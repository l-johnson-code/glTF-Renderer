#include "Rasterizer.h"

#include <algorithm>

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>

void Rasterizer::Init(ID3D12Device* device, RtvPool* rtv_allocator, DsvPool* dsv_allocator, CbvSrvUavPool* cbv_uav_srv_allocator, uint32_t width, uint32_t height)
{
    this->device = device;
    this->rtv_allocator = rtv_allocator;
    this->dsv_allocator = dsv_allocator;
    this->cbv_uav_srv_allocator = cbv_uav_srv_allocator;
    Resize(width, height);
    forward.Create(device);
    bloom.Create(this->device.Get(), width, height, 6);
}

void Rasterizer::Resize(uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = height;

    CD3DX12_HEAP_PROPERTIES render_target_heap_properties(D3D12_HEAP_TYPE_DEFAULT);

	HRESULT result;
	// Depth buffer.
    {
        CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height, 1, 1);
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        
        CD3DX12_CLEAR_VALUE clear_value(DXGI_FORMAT_D32_FLOAT, DEPTH_CLEAR_VALUE, 0);
        result = device->CreateCommittedResource(&render_target_heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear_value, IID_PPV_ARGS(this->depth.ReleaseAndGetAddressOf()));
        assert(result == S_OK);
        result = depth->SetName(L"Depth Texture");
        assert(result == S_OK);

        this->depth_dsv = dsv_allocator->AllocateAndCreateDsv(this->depth.Get(), nullptr);
        assert(this->depth_dsv.ptr != 0);

        CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT);
        this->depth_srv = cbv_uav_srv_allocator->AllocateAndCreateSrv(this->depth.Get(), &srv_desc);
        assert(this->depth_srv != -1);
    }

    // Motion vectors.
    {
        CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, width, height, 1, 1);
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        float clear_color[4] = {0.0, 0.0, 0.0, 0.0};
        CD3DX12_CLEAR_VALUE clear_value(DXGI_FORMAT_R16G16_FLOAT, clear_color);

        result = device->CreateCommittedResource(&render_target_heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clear_value, IID_PPV_ARGS(this->motion_vectors.ReleaseAndGetAddressOf()));
        assert(result == S_OK);
        result = this->motion_vectors->SetName(L"Motion Vectors");
        assert(result == S_OK);

        this->motion_vectors_rtv = rtv_allocator->AllocateAndCreateRtv(this->motion_vectors.Get(), nullptr);
        assert(this->motion_vectors_rtv.ptr != 0);
        this->motion_vectors_srv = cbv_uav_srv_allocator->AllocateAndCreateSrv(this->motion_vectors.Get(), nullptr);
        assert(this->motion_vectors_srv != -1);
    }

    // Transmission.
    {
        CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1);
        resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                    
        result = device->CreateCommittedResource(&render_target_heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(this->transmission.ReleaseAndGetAddressOf()));
        assert(result == S_OK);
        result = this->transmission->SetName(L"Transmission");
        assert(result == S_OK);

        this->transmission_srv = cbv_uav_srv_allocator->AllocateAndCreateSrv(this->transmission.Get(), nullptr);
        assert(this->transmission_srv != -1);
    }
}

void Rasterizer::GatherRenderObjects(Gltf* gltf, int scene)
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

void Rasterizer::SortRenderObjects(glm::vec3 camera_pos)
{
	auto comparison = [&](const RenderObject& a, const RenderObject& b) -> bool {
		glm::vec3 pos_a = glm::vec3(a.transform[3]) - camera_pos;
		glm::vec3 pos_b = glm::vec3(b.transform[3]) - camera_pos;
		return (glm::dot(pos_a, pos_a)) > (glm::dot(pos_b, pos_b));
	};
	std::sort(alpha_render_objects.begin(), alpha_render_objects.end(), comparison);
	std::sort(transparent_render_objects.begin(), transparent_render_objects.end(), comparison);
}

void Rasterizer::DrawRenderObjects(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* frame_allocator, Gltf* gltf, const std::vector<RenderObject>& render_objects)
{
	for (auto& render_object: render_objects) {
		DynamicMesh* dynamic_mesh = render_object.dynamic_mesh_id != -1 ? &gltf->dynamic_primitives[render_object.dynamic_mesh_id].dynamic_meshes[render_object.primitive_id] : nullptr;
		forward.Draw(
			command_list,
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

void Rasterizer::SetViewportAndScissorRects(ID3D12GraphicsCommandList* command_list, int width, int height)
{
	CD3DX12_VIEWPORT viewport(0.0f, 0.0f, width, height);
	command_list->RSSetViewports(1, &viewport);
	CD3DX12_RECT scissor_rect(0, 0, width, height);
	command_list->RSSetScissorRects(1, &scissor_rect);
}

void Rasterizer::DrawScene(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* frame_allocator, CbvSrvUavStack* descriptor_allocator, const Settings* settings, const ExecuteParams* execute_params)
{
    // Get transform matrices.
	glm::mat4x4 world_to_view = execute_params->camera->GetWorldToView();
	glm::mat4x4 world_to_clip = execute_params->camera->GetViewToClip() * world_to_view;
	glm::mat4x4 view_to_world = glm::affineInverse(world_to_view);
	glm::mat4x4 clip_to_world = glm::inverse(world_to_clip);
	glm::vec3 camera_pos = view_to_world[3];
    
    // Gather everything to draw.
	GatherRenderObjects(execute_params->gltf, execute_params->scene);
	SortRenderObjects(camera_pos);

	// Prepare render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE render_rtv = execute_params->output_rtv; 

	CD3DX12_RESOURCE_BARRIER barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			execute_params->output_resource, 
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
	command_list->ClearDepthStencilView(depth_dsv, D3D12_CLEAR_FLAG_DEPTH, DEPTH_CLEAR_VALUE, 0, 0, nullptr);
	
	SetViewportAndScissorRects(command_list, this->width, this->height);

	// Render opaque objects.
	ForwardPass::Config config = {
		.width = (int)this->width,
		.height = (int)this->height,
		.world_to_clip = world_to_clip,
		.previous_world_to_clip = this->previous_world_to_clip,
		.camera_pos = camera_pos,
		.num_of_lights = execute_params->light_count,
		.lights = execute_params->gpu_lights,
		.materials = execute_params->gpu_materials,
		.ggx_cube_descriptor = execute_params->environment_map ? execute_params->environment_map->ggx_srv_descriptor : -1,
		.diffuse_cube_descriptor = execute_params->environment_map ? execute_params->environment_map->diffuse_srv_descriptor : -1,
		.environment_map_intensity = 1.0,
		.transmission_descriptor = -1,
		.render_flags = settings->render_flags,
	};
	D3D12_PRIMITIVE_TOPOLOGY primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	command_list->IASetPrimitiveTopology(primitive_topology);
	forward.SetRootSignature(command_list);
	forward.SetConfig(command_list, frame_allocator, &config);
	forward.BindRenderTargets(command_list, render_rtv, motion_vectors_rtv, depth_dsv);
	forward.BindPipeline(command_list, ForwardPass::PIPELINE_FLAGS_NONE);
	DrawRenderObjects(command_list, frame_allocator, execute_params->gltf, opaque_render_objects);

	// TODO: Create a separate pipeline for alpha mask instead of sharing the opaque pass. This could potentially improve performance of the opaque rendering.
	DrawRenderObjects(command_list, frame_allocator, execute_params->gltf, alpha_mask_render_objects);

	if (execute_params->environment_map) {
		forward.DrawBackground(command_list, frame_allocator, clip_to_world, 1.0, execute_params->environment_map->cube_srv_descriptor);
		
		// Set pipeline state back to rendering meshes.
		forward.SetRootSignature(command_list);
		forward.SetConfig(command_list, frame_allocator, &config);
	}

	// Create transmission mip chain.
	{
		CD3DX12_RESOURCE_BARRIER resource_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			execute_params->output_resource, 
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_COPY_SOURCE
		);
		command_list->ResourceBarrier(1, &resource_barrier);
	}
	forward.GenerateTransmissionMips(command_list, frame_allocator, descriptor_allocator, execute_params->output_resource, this->transmission.Get(), settings->transmission_downsample_sample_pattern);
	{
		CD3DX12_RESOURCE_BARRIER resource_barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			execute_params->output_resource, 
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		);
		command_list->ResourceBarrier(1, &resource_barrier);
	}

	config.transmission_descriptor = this->transmission_srv;
	forward.SetConfig(command_list, frame_allocator, &config);

	// Render transmissives.
	forward.BindPipeline(command_list, ForwardPass::PIPELINE_FLAGS_ALPHA_BLEND);
	DrawRenderObjects(command_list, frame_allocator, execute_params->gltf, transparent_render_objects);

	// Render alpha blended geometry.
	DrawRenderObjects(command_list, frame_allocator, execute_params->gltf, alpha_render_objects);

	// Transition render targets to read state for post processing.
	CD3DX12_RESOURCE_BARRIER resource_barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			execute_params->output_resource, 
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

    bloom.Execute(command_list, frame_allocator, descriptor_allocator, execute_params->output_resource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, settings->bloom_radius, settings->bloom_strength);
    {
        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(execute_params->output_resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        command_list->ResourceBarrier(1, &barrier);
    }

	this->previous_world_to_clip = world_to_clip;
}

void Rasterizer::Shutdown()
{
    device.Reset();
    if (dsv_allocator) {
        dsv_allocator->Free(depth_dsv);
        depth_dsv = {0};
    }
    if (rtv_allocator) {
        rtv_allocator->Free(motion_vectors_rtv);
        motion_vectors_rtv = {0};
    }
    if (cbv_uav_srv_allocator) {
        cbv_uav_srv_allocator->Free(depth_srv);
        depth_srv = -1;
        cbv_uav_srv_allocator->Free(motion_vectors_srv);
        motion_vectors_srv = -1;
        cbv_uav_srv_allocator->Free(transmission_srv);
        transmission_srv = -1;
    }
    rtv_allocator = nullptr;
    dsv_allocator = nullptr;
    cbv_uav_srv_allocator = nullptr;
    forward.Destroy();
}