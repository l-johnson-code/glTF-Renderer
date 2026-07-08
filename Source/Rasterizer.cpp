#include "Rasterizer.h"
#include <algorithm>

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>

#include "DebugDraw.h"

void Rasterizer::Init(Gpu::Resources* gpu_resources, uint16_t width, uint16_t height)
{
	this->gpu_resources = gpu_resources;
    Resize(width, height);
    forward.Create(gpu_resources);
    bloom.Create(gpu_resources, width, height, 6);
}

void Rasterizer::Resize(uint16_t width, uint16_t height)
{
    this->width = width;
    this->height = height;

    gpu_resources->FreeTexture(&this->depth);
    gpu_resources->FreeTexture(&this->motion_vectors);
    gpu_resources->FreeTexture(&this->transmission);

	HRESULT result;

	Gpu::TextureDesc depth_target_desc = {
		.name = "Depth",
		.format = DXGI_FORMAT_D32_FLOAT,
		.width = width,
		.height = height,
		.mip_levels = 1,
		.clear_depth = 0.0f,
		.flags = Gpu::TEXTURE_FLAG_SRV | Gpu::TEXTURE_FLAG_DEPTH_TARGET,
	};
    result = gpu_resources->CreateTexture(&depth_target_desc, &this->depth);
	
	Gpu::TextureDesc motion_vectors_desc = {
		.name = "Motion Vectors",
		.format = DXGI_FORMAT_R16G16_FLOAT,
		.width = width,
		.height = height,
		.mip_levels = 1,
		.clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
		.flags = Gpu::TEXTURE_FLAG_SRV | Gpu::TEXTURE_FLAG_RENDER_TARGET,
	};
	result = gpu_resources->CreateTexture(&motion_vectors_desc, &this->motion_vectors);

	Gpu::TextureDesc transmission_desc = {
		.name = "Transmission",
		.format = DXGI_FORMAT_R16G16B16A16_FLOAT,
		.width = width,
		.height = height,
		.mip_levels = 0,
		.flags = Gpu::TEXTURE_FLAG_SRV | Gpu::TEXTURE_FLAG_UAV | Gpu::TEXTURE_FLAG_SRV_PER_MIP,
	};
	result = gpu_resources->CreateTexture(&transmission_desc, &this->transmission);
}

void Rasterizer::GatherRenderObjects(Gltf* gltf, int scene, glm::mat4x4 world_to_clip, const Settings* settings)
{
	opaque_render_objects.clear();
	alpha_mask_render_objects.clear();
	alpha_render_objects.clear();
	transparent_render_objects.clear();

	FrustumPlanes frustum_planes = ExtractPlanesFromMatrix(world_to_clip);

	gltf->TraverseScene(scene, [&](Gltf* gltf, int node_id) {
		const Gltf::Node& node = gltf->nodes[node_id];
		if (node.mesh_id != -1) {
			const Gltf::Mesh& mesh = gltf->meshes[node.mesh_id];
			for (int i = 0; i < mesh.primitives.size(); i++) {

				// Frustum culling.
				if (settings->frustum_culling) {
					Obb obb = ObbFromAabb(mesh.primitives[i].aabb, node.global_transform);
					bool cull = OutsideFrustum(frustum_planes, obb);
					if (cull) {
						continue;
					}
				}

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

				// Draw bounding boxes.
				if (settings->draw_bounding_boxes) {
					DebugDraw::DrawBox(mesh.primitives[i].aabb.start, mesh.primitives[i].aabb.end, node.global_transform, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
				}

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

void Rasterizer::DrawRenderObjects(CommandContext* context, Gltf* gltf, const std::vector<RenderObject>& render_objects)
{
	for (auto& render_object: render_objects) {
		DynamicMesh* dynamic_mesh = render_object.dynamic_mesh_id != -1 ? &gltf->dynamic_primitives[render_object.dynamic_mesh_id].dynamic_meshes[render_object.primitive_id] : nullptr;
		forward.Draw(
			context,
			&gltf->meshes[render_object.mesh_id].primitives[render_object.primitive_id].mesh,
			render_object.material_id,
			render_object.transform,
			render_object.normal_transform,
			render_object.previous_transform,
			dynamic_mesh
		);
	}
}

void Rasterizer::SetViewportAndScissorRects(CommandContext* context, int width, int height)
{
	CD3DX12_VIEWPORT viewport(0.0f, 0.0f, width, height);
	context->SetViewports(1, &viewport);
	CD3DX12_RECT scissor_rect(0, 0, width, height);
	context->SetScissorRects(1, &scissor_rect);
}

void Rasterizer::DrawScene(CommandContext* context, const Settings* settings, const ExecuteParams* execute_params)
{
    // Get transform matrices.
	glm::mat4x4 world_to_view = execute_params->camera->GetWorldToView();
	glm::mat4x4 world_to_clip = execute_params->camera->GetViewToClip() * world_to_view;
	glm::mat4x4 view_to_world = glm::affineInverse(world_to_view);
	glm::mat4x4 clip_to_world = glm::inverse(world_to_clip);
	glm::vec3 camera_pos = view_to_world[3];
    
    // Gather everything to draw.
	GatherRenderObjects(execute_params->gltf, execute_params->scene, world_to_clip, settings);
	SortRenderObjects(camera_pos);

	// Prepare render targets.
	D3D12_CPU_DESCRIPTOR_HANDLE render_rtv = execute_params->output->Rtv(); 

	context->PushTransitionBarrier(
		execute_params->output->Resource(), 
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	context->PushTransitionBarrier(
		motion_vectors.Resource(), 
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	context->PushTransitionBarrier(
		depth.Resource(), 
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 
		D3D12_RESOURCE_STATE_DEPTH_WRITE
	);
	context->SubmitBarriers();
	
	float clear_color[4] = {0., 0., 0., 0.};
	context->ClearRenderTargetView(render_rtv, clear_color);
	context->ClearRenderTargetView(motion_vectors.Rtv(), motion_vectors.ClearColor());
	context->ClearDepthStencilView(depth.Dsv(), depth.ClearDepth());
	
	SetViewportAndScissorRects(context, this->width, this->height);

	// Render opaque objects.
	context->BeginEvent("Opaque");
	ForwardPass::Config config = {
		.width = (int)this->width,
		.height = (int)this->height,
		.world_to_clip = world_to_clip,
		.previous_world_to_clip = this->previous_world_to_clip,
		.camera_pos = camera_pos,
		.num_of_lights = execute_params->gpu_scene->LightCount(),
		.lights = &execute_params->gpu_scene->LightBuffer(),
		.materials = &execute_params->gpu_scene->MaterialBuffer(),
		.ggx_cube_descriptor = execute_params->environment_map ? execute_params->environment_map->ggx.Srv() : -1,
		.diffuse_cube_descriptor = execute_params->environment_map ? execute_params->environment_map->diffuse.Srv() : -1,
		.environment_map_intensity = 1.0,
		.transmission_descriptor = -1,
		.render_flags = settings->render_flags,
	};
	D3D12_PRIMITIVE_TOPOLOGY primitive_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	context->SetPrimitiveTopology(primitive_topology);
	forward.SetRootSignature(context);
	forward.SetConfig(context, &config);
	forward.BindRenderTargets(context, render_rtv, motion_vectors.Rtv(), depth.Dsv());
	forward.BindPipeline(context, ForwardPass::PIPELINE_FLAGS_NONE);
	DrawRenderObjects(context, execute_params->gltf, opaque_render_objects);
	context->EndEvent();

	context->BeginEvent("Alpha Tested");
	// TODO: Create a separate pipeline for alpha mask instead of sharing the opaque pass. This could potentially improve performance of the opaque rendering.
	DrawRenderObjects(context, execute_params->gltf, alpha_mask_render_objects);
	context->EndEvent();

	context->BeginEvent("Background");
	if (execute_params->environment_map) {
		forward.DrawBackground(context, clip_to_world, 1.0, execute_params->environment_map->cube.Srv());
		
		// Set pipeline state back to rendering meshes.
		forward.SetRootSignature(context);
		forward.SetConfig(context, &config);
	}
	context->EndEvent();

	// Create transmission mip chain.
	context->BeginEvent("Transmission Mip Chain");
	context->PushTransitionBarrier(
		execute_params->output->Resource(), 
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COPY_SOURCE
	);
	context->SubmitBarriers();
	forward.GenerateTransmissionMips(context, execute_params->output, &this->transmission, settings->transmission_downsample_sample_pattern);
	context->PushTransitionBarrier(
		execute_params->output->Resource(),
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	context->SubmitBarriers();
	context->EndEvent();

	config.transmission_descriptor = this->transmission.Srv();
	forward.SetConfig(context, &config);

	// Render transmissives.
	context->BeginEvent("Transmissive");
	forward.BindPipeline(context, ForwardPass::PIPELINE_FLAGS_ALPHA_BLEND);
	DrawRenderObjects(context, execute_params->gltf, transparent_render_objects);
	context->EndEvent();
	
	// Render alpha blended geometry.
	context->BeginEvent("Alpha Blended");
	DrawRenderObjects(context, execute_params->gltf, alpha_render_objects);
	context->EndEvent();

	// Transition render targets to read state for post processing.
	context->PushTransitionBarrier(
		execute_params->output->Resource(), 
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	);
	context->PushTransitionBarrier(
		motion_vectors.Resource(), 
		D3D12_RESOURCE_STATE_RENDER_TARGET, 
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	);
	context->PushTransitionBarrier(
		depth.Resource(), 
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	);
	context->SubmitBarriers();

	context->BeginEvent("Bloom");
    bloom.Execute(context, execute_params->output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, settings->bloom_radius, settings->bloom_strength);
	context->EndEvent();

	context->PushTransitionBarrier(execute_params->output->Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context->SubmitBarriers();

	this->previous_world_to_clip = world_to_clip;
}

void Rasterizer::Shutdown()
{
	if (gpu_resources) {
		gpu_resources->FreeTexture(&depth);
		gpu_resources->FreeTexture(&motion_vectors);
		gpu_resources->FreeTexture(&transmission);
	}
    forward.Destroy();
}