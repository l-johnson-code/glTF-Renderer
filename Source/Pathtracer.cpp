#include "Pathtracer.h"

#include <algorithm>
#include <cassert>

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>

#include "GpuResources.h"

void Pathtracer::Init(ID3D12Device5* device, UploadBuffer* upload_buffer)
{
    HRESULT result = S_OK;

    CD3DX12_ROOT_PARAMETER root_parameters[ROOT_PARAMETER_COUNT];
    root_parameters[ROOT_PARAMETER_CONSTANT_BUFFER].InitAsConstantBufferView(0);
    root_parameters[ROOT_PARAMETER_ACCELERATION_STRUCTURE].InitAsShaderResourceView(0);
    root_parameters[ROOT_PARAMETER_INSTANCES].InitAsShaderResourceView(1);
    root_parameters[ROOT_PARAMETER_MATERIALS].InitAsShaderResourceView(2);
    root_parameters[ROOT_PARAMETER_LIGHTS].InitAsShaderResourceView(3);

    CD3DX12_STATIC_SAMPLER_DESC static_samplers[] = {
        CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
    };

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(ROOT_PARAMETER_COUNT, root_parameters, std::size(static_samplers), static_samplers);
    root_signature_desc.Flags = 
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | 
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    
    Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> root_signature_error_blob;
    result = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &root_signature_blob, &root_signature_error_blob);
    assert(SUCCEEDED(result));
    result = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&this->root_signature));
    assert(SUCCEEDED(result));

    // Root signature shared between all shaders.
    D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {
        .pGlobalRootSignature = this->root_signature.Get(),
    };

    // Shader code.
    D3D12_DXIL_LIBRARY_DESC dxil_library_desc = {
        .DXILLibrary = GpuResources::LoadShader("Shaders/PathTracer.lib.bin"),
        .NumExports = 0,
        .pExports = nullptr,
    };

    // Choose which functions form a hitgroup.
    D3D12_HIT_GROUP_DESC hit_group_desc = {
        .HitGroupExport = L"HitGroup",
        .Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
        .AnyHitShaderImport = L"AnyHit",
        .ClosestHitShaderImport = L"ClosestHit",
        .IntersectionShaderImport = nullptr,
    };

    D3D12_HIT_GROUP_DESC shadow_hit_group_desc = {
        .HitGroupExport = L"ShadowHitGroup",
        .Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
        .AnyHitShaderImport = L"ShadowAnyHit",
        .ClosestHitShaderImport = nullptr,
        .IntersectionShaderImport = nullptr,
    };

    D3D12_RAYTRACING_SHADER_CONFIG raytracing_shader_config = {
        .MaxPayloadSizeInBytes = sizeof(float) * 10,
        .MaxAttributeSizeInBytes = sizeof(float) * 2, // Size of BuiltInTriangleIntersectionAttributes.
    };

    D3D12_RAYTRACING_PIPELINE_CONFIG raytracing_pipeline_config = {
        .MaxTraceRecursionDepth = MAX_BOUNCES + 2,
    };

    D3D12_STATE_SUBOBJECT subobjects[] = {
        {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root_signature},
        {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &dxil_library_desc},
        {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group_desc},
        {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &shadow_hit_group_desc},
        {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &raytracing_shader_config},
        {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &raytracing_pipeline_config},
    };

    D3D12_STATE_OBJECT_DESC state_object_desc = {
        .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
        .NumSubobjects = std::size(subobjects),
        .pSubobjects = subobjects,
    };
    result = device->CreateStateObject(&state_object_desc, IID_PPV_ARGS(&this->state_object));
    assert(SUCCEEDED(result));

    // Create the shader table.
    int shader_table_size = ShaderTableCollectionBuilder::CalculateRequiredSize(1, 1, 0);
    CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(shader_table_size, D3D12_RESOURCE_FLAG_NONE, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&this->shader_tables_resource));
    assert(SUCCEEDED(result));
    void* shader_tables_data = upload_buffer->QueueBufferUpload(shader_table_size, this->shader_tables_resource.Get(), 0);
    assert(shader_tables_data);
    
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> state_object_properties;
    result = state_object.As(&state_object_properties);
    assert(SUCCEEDED(result));
    void* ray_generation_identifier = state_object_properties->GetShaderIdentifier(L"RayGeneration");
    void* hit_group_identifier = state_object_properties->GetShaderIdentifier(L"HitGroup");
    void* shadow_hit_group_identifier = state_object_properties->GetShaderIdentifier(L"ShadowHitGroup");
    void* miss_identifier = state_object_properties->GetShaderIdentifier(L"Miss");
    void* shadow_miss_identifier = state_object_properties->GetShaderIdentifier(L"ShadowMiss");

    ShaderTableCollectionBuilder collection_builder;
    collection_builder.Create(shader_tables_data, MISS_SHADER_COUNT, HIT_GROUP_COUNT, 0);
    collection_builder.ray_generation_record.SetShader(ray_generation_identifier);
    collection_builder.miss_table.SetShader(MISS_SHADER_BOUNCE, miss_identifier);
    collection_builder.miss_table.SetShader(MISS_SHADER_SHADOW, shadow_miss_identifier);
    collection_builder.hit_group_table.SetShader(HIT_GROUP_BOUNCE, hit_group_identifier);
    collection_builder.hit_group_table.SetShader(HIT_GROUP_SHADOW, shadow_hit_group_identifier);
    this->shader_tables = collection_builder.GetShaderTableCollection(this->shader_tables_resource->GetGPUVirtualAddress());

    acceleration_structure.Init(device, Config::MAX_BLAS_VERTICES, Config::MAX_TLAS_INSTANCES);

    // Cleanup.
    GpuResources::FreeShader(dxil_library_desc.DXILLibrary);
}

void Pathtracer::Shutdown()
{
    root_signature.Reset();
    state_object.Reset();
    shader_tables_resource.Reset();
}

void Pathtracer::BuildAllBlas(Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list)
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

void Pathtracer::UpdateAllBlas(Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list)
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

void Pathtracer::BuildTlas(Gltf* gltf, int scene_id, RaytracingAccelerationStructure* acceleration_structure, ID3D12GraphicsCommandList4* command_list, CpuMappedLinearBuffer* allocator)
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

void Pathtracer::PathtraceScene(ID3D12GraphicsCommandList4* command_list, CpuMappedLinearBuffer* frame_allocator, CbvSrvUavStack* descriptor_allocator, const Settings* settings, const ExecuteParams* execute_params)
{
	glm::mat4x4 world_to_view = execute_params->camera->GetWorldToView();
	glm::mat4x4 world_to_clip = execute_params->camera->GetViewToClip() * world_to_view;
	glm::mat4x4 view_to_world = glm::affineInverse(world_to_view);
	glm::mat4x4 clip_to_world = glm::inverse(world_to_clip);
	glm::vec3 camera_pos = view_to_world[3];

    bool reset = (world_to_clip != previous_world_to_clip) || (settings->reset);
    // Reset accumulation if the camera position has changed.
    if (reset) {
        this->accumulated_frames = 0;
    }

	if (accumulated_frames < settings->max_accumulated_frames) {
        
        // Update the acceleration structure.
		BuildAllBlas(execute_params->gltf, &this->acceleration_structure, command_list);
		UpdateAllBlas(execute_params->gltf, &this->acceleration_structure, command_list);
		BuildTlas(execute_params->gltf, execute_params->scene, &this->acceleration_structure, command_list, frame_allocator);
        
        struct {
            glm::mat4x4 clip_to_world;
            glm::vec3 camera_pos;
            int num_of_lights;
            uint32_t width;
            uint32_t height;
            uint32_t seed;
            int accumulated_frames;
            glm::vec3 environment_color;
            float environment_intensity;
            int debug_output;
            uint32_t flags;
            float max_ray_length;
            int min_bounces;
            int max_bounces;
            int output_descriptor;
            int environment_map_descriptor_id;
            int environment_importance_map_descriptor_id;
            float luminance_clamp;
            float min_russian_roulette_continue_prob;
            float max_russian_roulette_continue_prob;
        } constants;

        constants = {
            .clip_to_world = clip_to_world,
            .camera_pos = camera_pos,
            .num_of_lights = execute_params->light_count,
            .width = execute_params->width,
            .height = execute_params->height,
            .seed = settings->use_frame_as_seed ? (uint32_t)execute_params->frame : settings->seed,
            .accumulated_frames = this->accumulated_frames,
            .environment_color = settings->environment_color,
            .environment_intensity = settings->environment_intensity,
            .debug_output = settings->debug_output,
            .flags = settings->flags,
            .max_ray_length = 1000,
            .min_bounces = std::clamp(settings->min_bounces, 0, MAX_BOUNCES),
            .max_bounces = std::clamp(settings->max_bounces, 0, MAX_BOUNCES),
            .output_descriptor = execute_params->output_descriptor,
            .environment_map_descriptor_id = execute_params->environment_map ? execute_params->environment_map->cube_srv_descriptor : -1,
            .environment_importance_map_descriptor_id = execute_params->environment_map ? execute_params->environment_map->importance_srv_descriptor : -1,
            .luminance_clamp = settings->luminance_clamp,
            .min_russian_roulette_continue_prob = settings->min_russian_roulette_continue_prob,
            .max_russian_roulette_continue_prob = settings->max_russian_roulette_continue_prob,
        };

        D3D12_GPU_VIRTUAL_ADDRESS constant_buffer = frame_allocator->Copy(&constants, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        command_list->SetComputeRootSignature(this->root_signature.Get());
        command_list->SetComputeRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER, constant_buffer);
        command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_ACCELERATION_STRUCTURE, this->acceleration_structure.GetAccelerationStructure());
        command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_INSTANCES, this->gpu_mesh_instances);
        command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_MATERIALS, execute_params->gpu_materials);
        command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_LIGHTS, execute_params->gpu_lights);

        command_list->SetPipelineState1(this->state_object.Get());

        D3D12_DISPATCH_RAYS_DESC desc = {
            .RayGenerationShaderRecord = this->shader_tables.ray_generation_shader_record,
            .MissShaderTable = this->shader_tables.miss_shader_table,
            .HitGroupTable = this->shader_tables.hit_group_table,
            .CallableShaderTable = this->shader_tables.callable_shader_table,
            .Width = execute_params->width,
            .Height = execute_params->height,
            .Depth = 1,
        };
        command_list->DispatchRays(&desc);

        if (settings->flags & FLAG_ACCUMULATE) {
            this->accumulated_frames++;
        } else {
            this->accumulated_frames = 0;
        }
		
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(execute_params->output_resource);
		command_list->ResourceBarrier(1, &barrier);
	}
	
	previous_world_to_clip = world_to_clip;
}
