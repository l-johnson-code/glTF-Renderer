#include "Pathtracer.h"

#include <algorithm>
#include <cassert>

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>

#include "GpuResources.h"

static uint8_t ReverseBits8(uint8_t bits)
{
    bits = (bits << 4) | (bits >> 4);
    bits = ((bits & 0x33) << 2) | ((bits & 0xcc) >> 2);
    bits = ((bits & 0x55) << 1) | ((bits & 0xaa) >> 1);
    return bits;
}

static uint16_t ReverseBits16(uint16_t bits)
{
    return (ReverseBits8(bits) << 8) | ReverseBits8(bits >> 8);
}

static float RadicalInverseBase3(uint16_t number)
{
    float result = 0.0f;
    float inverse_base = 1.0f / 3.0f;
    while (number > 0) {
        result += (float)(number % 3) * inverse_base;
        inverse_base *= inverse_base;
        number /= 3;
    }
    return result;
}

static glm::vec2 HaltonSequence(uint16_t i)
{
    glm::vec2 result;
    result.x = ReverseBits16(i) * 0x1p-16f;
    result.y = RadicalInverseBase3(i);
    return result;
}

void Pathtracer::Init(ID3D12Device5* device, Gpu::Resources* resources, UploadBuffer* upload_buffer, uint32_t width, uint32_t height)
{
    HRESULT result = S_OK;

    this->resources = resources;

    // Root signature shared between all shaders.
    D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {
        .pGlobalRootSignature = resources->GenericComputeRootSignature(),
    };

    // Shader code.
    D3D12_DXIL_LIBRARY_DESC dxil_library_desc = {
        .DXILLibrary = Gpu::Resources::LoadShader("Shaders/PathTracer.lib.bin"),
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
        .MaxPayloadSizeInBytes = sizeof(float) * 5,
        .MaxAttributeSizeInBytes = sizeof(float) * 2, // Size of BuiltInTriangleIntersectionAttributes.
    };

    D3D12_RAYTRACING_PIPELINE_CONFIG raytracing_pipeline_config = {
        .MaxTraceRecursionDepth = 1,
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
    result = resources->CreateStateObject(&state_object_desc, &this->state_object, "Path Tracer State Object");
    assert(SUCCEEDED(result));

    // Create the shader table.
    uint64_t shader_table_size = ShaderTableCollectionBuilder::CalculateRequiredSize(MISS_SHADER_COUNT, HIT_GROUP_COUNT, 0);
    Gpu::BufferDesc shader_table_buffer_desc = {
        .name = "Shader Table",
        .size = shader_table_size,
    };
    result = resources->CreateBuffer(&shader_table_buffer_desc, &this->shader_tables_buffer);
    assert(SUCCEEDED(result));

    void* shader_tables_data = upload_buffer->QueueBufferUpload(shader_table_size, this->shader_tables_buffer.Resource(), 0);
    assert(shader_tables_data);
    
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> state_object_properties;
    result = state_object.As(&state_object_properties);
    assert(SUCCEEDED(result));
    void* ray_generation_identifier = state_object_properties->GetShaderIdentifier(L"RayGeneration");
    void* hit_group_identifier = state_object_properties->GetShaderIdentifier(L"HitGroup");
    void* shadow_hit_group_identifier = state_object_properties->GetShaderIdentifier(L"ShadowHitGroup");
    void* shadow_miss_identifier = state_object_properties->GetShaderIdentifier(L"ShadowMiss");

    ShaderTableCollectionBuilder collection_builder;
    collection_builder.Create(shader_tables_data, MISS_SHADER_COUNT, HIT_GROUP_COUNT, 0);
    collection_builder.ray_generation_record.SetShader(ray_generation_identifier);
    collection_builder.miss_table.SetShader(MISS_SHADER_SHADOW, shadow_miss_identifier);
    collection_builder.hit_group_table.SetShader(HIT_GROUP_BOUNCE, hit_group_identifier);
    collection_builder.hit_group_table.SetShader(HIT_GROUP_SHADOW, shadow_hit_group_identifier);
    this->shader_tables = collection_builder.GetShaderTableCollection(this->shader_tables_buffer.Resource()->GetGPUVirtualAddress());

    acceleration_structure.Init(device, resources, Config::MAX_BLAS_VERTICES, Config::MAX_TLAS_INSTANCES);

    Resize(width, height);
    CreateVBufferPipelines();

    for (int i = 0; i < gpu_mesh_instances.Size(); i++) {
        Gpu::BufferDesc instances_buffer_desc = {
            .name = "Instances",
            .size = sizeof(GpuMeshInstance) * MAX_INSTANCES,
            .flags = Gpu::BUFFER_FLAG_GENERATE_DESCRIPTOR | Gpu::BUFFER_FLAG_PERSISTENT_MAP,
            .structured_byte_stride = sizeof(GpuMeshInstance),
            .heap_type = D3D12_HEAP_TYPE_UPLOAD,
        };
        result = resources->CreateBuffer(&instances_buffer_desc, &gpu_mesh_instances[i]);
        assert(SUCCEEDED(result));
    }

    // Cleanup.
    Gpu::Resources::FreeShader(dxil_library_desc.DXILLibrary);
}

void Pathtracer::Resize(uint32_t width, uint32_t height)
{
    // Release visibility buffer.
    this->resources->FreeTexture(&v_buffer_primitive);
    this->resources->FreeTexture(&v_buffer_instance);
    this->resources->FreeTexture(&v_buffer_depth);

    // Create visibility buffer.
    HRESULT result = S_OK;
    Gpu::TextureDesc primitive_id_desc = {
        .name = "V Buffer Primitive ID",
        .format = DXGI_FORMAT_R32_UINT,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .mip_levels = 1,
        .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
        .flags = Gpu::TEXTURE_FLAG_RENDER_TARGET | Gpu::TEXTURE_FLAG_SRV,
        .initial_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    };
    result = resources->CreateTexture(&primitive_id_desc, &this->v_buffer_primitive);
    assert(SUCCEEDED(result));

    Gpu::TextureDesc instance_desc = {
        .name = "V Buffer Instance",
        .format = DXGI_FORMAT_R32_UINT,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .mip_levels = 1,
        .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
        .flags = Gpu::TEXTURE_FLAG_RENDER_TARGET | Gpu::TEXTURE_FLAG_SRV,
        .initial_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
    };
    result = resources->CreateTexture(&instance_desc, &this->v_buffer_instance);
    assert(SUCCEEDED(result));

    Gpu::TextureDesc depth_desc = {
        .name = "V Buffer Depth",
        .format = DXGI_FORMAT_D32_FLOAT,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .mip_levels = 1,
        .clear_depth = 0.0f,
        .flags = Gpu::TEXTURE_FLAG_DEPTH_TARGET,
        .initial_state = D3D12_RESOURCE_STATE_DEPTH_WRITE,
    };
    result = resources->CreateTexture(&depth_desc, &this->v_buffer_depth);
    assert(SUCCEEDED(result));

    this->width = width;
    this->height = height;
}

void Pathtracer::Shutdown()
{
    state_object.Reset();
    if (resources) {
        resources->FreeBuffer(&shader_tables_buffer);
        for (int i = 0; i < gpu_mesh_instances.Size(); i++) {
            resources->FreeBuffer(&gpu_mesh_instances[i]);
        }
    }
}

void Pathtracer::CreateVBufferPipelines()
{
    HRESULT result = S_OK;

    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	Gpu::GraphicsPipelineDesc pipeline_desc = {
        .name = "Visibility",
        .vertex_shader = "Visibility",
        .pixel_shader = "Visibility",
        .rasterizer_state = {
            .FillMode = D3D12_FILL_MODE_SOLID,
            .CullMode = D3D12_CULL_MODE_NONE,
            .FrontCounterClockwise = TRUE,
            .DepthClipEnable = TRUE,
            .MultisampleEnable = FALSE,
            .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
        },
        .depth_stencil_state = {
            .DepthEnable = TRUE,
            .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
            .DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
        },
        .input_layout = {
            .pInputElementDescs = input_layout,
            .NumElements = std::size(input_layout),
        },
        .primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .render_target_count = 2,
        .render_target_formats = {
            DXGI_FORMAT_R32_UINT,
            DXGI_FORMAT_R32_UINT,
        },
        .depth_stencil_format = DXGI_FORMAT_D32_FLOAT
    };

	result = this->resources->CreateGraphicsPipelineState(&pipeline_desc, &this->v_buffer_pipeline);
	assert(result == S_OK);

    D3D12_INPUT_ELEMENT_DESC alpha_tested_input_layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R16G16B16A16_UNORM, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
    
    Gpu::GraphicsPipelineDesc alpha_tested_pipeline_desc = {
        .name = "Visibility Alpha Tested",
        .vertex_shader = "VisibilityAlphaTested",
        .pixel_shader = "VisibilityAlphaTested",
        .rasterizer_state = {
            .FillMode = D3D12_FILL_MODE_SOLID,
            .CullMode = D3D12_CULL_MODE_NONE,
            .FrontCounterClockwise = TRUE,
            .DepthClipEnable = TRUE,
            .MultisampleEnable = FALSE,
            .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
        },
        .depth_stencil_state = {
            .DepthEnable = TRUE,
            .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
            .DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
        },
        .input_layout = {
            .pInputElementDescs = alpha_tested_input_layout,
            .NumElements = std::size(alpha_tested_input_layout),
        },
        .primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .render_target_count = 2,
        .render_target_formats = {
            DXGI_FORMAT_R32_UINT,
            DXGI_FORMAT_R32_UINT,
        },
        .depth_stencil_format = DXGI_FORMAT_D32_FLOAT
    };

    result = this->resources->CreateGraphicsPipelineState(&alpha_tested_pipeline_desc, &this->v_buffer_alpha_tested_pipeline);
	assert(result == S_OK);
}

void Pathtracer::BuildAllBlas(CommandContext* context, Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure)
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
					if (!dynamic_blas.buffer.Resource()) {
						acceleration_structure->BuildDynamicBlas(context, primitive.mesh.position_and_tangent_space.view.BufferLocation, primitive.mesh.num_of_vertices, primitive.mesh.index.view, primitive.mesh.num_of_indices, &dynamic_blas);
					}
				} else {
					// Static.
					if (!primitive.blas.buffer.Resource()) {
						acceleration_structure->BuildStaticBlas(context, primitive.mesh.position_and_tangent_space.view.BufferLocation, primitive.mesh.num_of_vertices, primitive.mesh.index.view, primitive.mesh.num_of_indices, &primitive.blas);
					}
				}
			}
        }
    }
    acceleration_structure->EndBlasBuilds(context);
}

void Pathtracer::UpdateAllBlas(CommandContext* context, Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure)
{
    for (int i = 0; i < gltf->nodes.size(); i++) {
		Gltf::Node& node = gltf->nodes[i];
		int mesh_id = node.mesh_id;
		int skin_id = node.dynamic_mesh;
        if (mesh_id != -1 && skin_id != -1) {
			std::vector<Gltf::Primitive>& primitives = gltf->meshes[mesh_id].primitives; 
			Gltf::DynamicPrimitives& dynamic_primitives = gltf->dynamic_primitives[skin_id];
			for (int j = 0; j < dynamic_primitives.dynamic_blases.size(); j++) {
				acceleration_structure->UpdateDynamicBlas(context, &dynamic_primitives.dynamic_blases[j], dynamic_primitives.dynamic_meshes[j].GetCurrentPositionAndTangentSpaceBuffer()->view.BufferLocation, primitives[j].mesh.num_of_vertices, primitives[j].mesh.index.view, primitives[j].mesh.num_of_indices);
			}
        }
    }
    acceleration_structure->EndBlasBuilds(context);
}

void Pathtracer::BuildTlas(CommandContext* context, Gltf* gltf, RaytracingAccelerationStructure* acceleration_structure)
{
    mesh_instances.clear();
    vertex_buffers.clear();
    alpha_vertex_buffers.clear();
    gpu_mesh_instances.Next();
    acceleration_structure->BeginTlasBuild();

	// TODO: Define this somewhere else?
	enum InstanceMask {
		MASK_NONE = 1 << 0,
		MASK_ALPHA_BLEND = 1 << 1,
	};
	gltf->TraverseScene([&](Gltf* gltf, int node_id) {
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
					.position_and_tangent_space_descriptor = mesh.position_and_tangent_space.descriptor,
					.texcoord_descriptor = mesh.texcoords.descriptor,
					.color_descriptor = mesh.color.descriptor,
					.material_id = primitives[i].material_id,
				};
				unsigned int flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
				if (material.flags & Gltf::Material::FLAG_DOUBLE_SIDED) {
					flags |= D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
				}
				if (material.alpha_mode == Gltf::Material::ALPHA_MODE_MASK) {
					flags |= D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
				}
				unsigned int instance_mask = 0;
				if (material.alpha_mode == Gltf::Material::ALPHA_MODE_BLEND) {
					instance_mask = MASK_ALPHA_BLEND;
				} else {
					instance_mask = MASK_NONE;
				}
				bool tlas_added = false;
                if (mesh_instances.size() <= MAX_INSTANCES) {
                    if (gltf->nodes[node_id].dynamic_mesh != -1) {
                        if (gltf->dynamic_primitives[node.dynamic_mesh].dynamic_blases.size() > i) {
                            // Dynamic.
                            DynamicMesh& dynamic_mesh = gltf->dynamic_primitives[node.dynamic_mesh].dynamic_meshes[i];
                            RaytracingAccelerationStructure::DynamicBlas& dynamic_blas = gltf->dynamic_primitives[node.dynamic_mesh].dynamic_blases[i];
                            tlas_added = acceleration_structure->AddTlasInstance(&dynamic_blas, node.global_transform, instance_mask, flags);
                            if (dynamic_mesh.flags & DynamicMesh::Flags::FLAG_POSITION) {
                                gpu_mesh_instance.position_and_tangent_space_descriptor = dynamic_mesh.GetCurrentPositionAndTangentSpaceBuffer()->descriptor;
                            }
                            if (tlas_added) {
                                if (material.alpha_mode == Gltf::Material::ALPHA_MODE_MASK) {
                                    alpha_vertex_buffers.push_back({
                                        .instance_id = (uint32_t)mesh_instances.size(),
                                        .index_count = mesh.num_of_indices,
                                        .vertex_count = mesh.num_of_vertices,
                                        .index = mesh.index.view,
                                        .vertices = dynamic_mesh.flags & DynamicMesh::FLAG_POSITION ? dynamic_mesh.GetCurrentPositionAndTangentSpaceBuffer()->view : mesh.position_and_tangent_space.view,
                                        .tex_coords = mesh.texcoords.view,
                                        .color = mesh.color.view,
                                        .material_id = primitives[i].material_id,
                                    });
                                } else {
                                    vertex_buffers.push_back({
                                        .instance_id = (uint32_t)mesh_instances.size(),
                                        .index_count = mesh.num_of_indices,
                                        .vertex_count = mesh.num_of_vertices,
                                        .index = mesh.index.view,
                                        .vertices = dynamic_mesh.flags & DynamicMesh::FLAG_POSITION ? dynamic_mesh.GetCurrentPositionAndTangentSpaceBuffer()->view : mesh.position_and_tangent_space.view,
                                    });
                                }
                            }
                        }
                    } else {
                        // Static.
                        RaytracingAccelerationStructure::Blas& blas = primitives[i].blas;
                        tlas_added = acceleration_structure->AddTlasInstance(&blas, node.global_transform, instance_mask, flags);
                        if (tlas_added) {
                            if (material.alpha_mode == Gltf::Material::ALPHA_MODE_MASK) {
                                alpha_vertex_buffers.push_back({
                                    .instance_id = (uint32_t)mesh_instances.size(),
                                    .index_count = mesh.num_of_indices,
                                    .vertex_count = mesh.num_of_vertices,
                                    .index = mesh.index.view,
                                    .vertices = mesh.position_and_tangent_space.view,
                                    .tex_coords = mesh.texcoords.view,
                                    .color = mesh.color.view,
                                    .material_id = primitives[i].material_id,
                                });
                            } else {
                                vertex_buffers.push_back({
                                    .instance_id = (uint32_t)mesh_instances.size(),
                                    .index_count = mesh.num_of_indices,
                                    .vertex_count = mesh.num_of_vertices,
                                    .index = mesh.index.view,
                                    .vertices = mesh.position_and_tangent_space.view,
                                });
                            }
                        }
                    }
                    if (tlas_added) {
                        mesh_instances.push_back(gpu_mesh_instance);
                    }
                }
			}
		}
	});

    acceleration_structure->BuildTlas(context);
	memcpy(this->gpu_mesh_instances.Current().Pointer(), mesh_instances.data(), sizeof(GpuMeshInstance) * mesh_instances.size());
}

void Pathtracer::PathtraceScene(CommandContext* context, const Settings* settings, const ExecuteParams* execute_params)
{
    glm::mat4x4 world_to_view = execute_params->camera->GetWorldToView();
	glm::mat4x4 view_to_clip = execute_params->camera->GetViewToClip();
    glm::mat4x4 world_to_clip = view_to_clip * world_to_view;
    bool reset = (world_to_clip != previous_world_to_clip) || (settings->reset);
    previous_world_to_clip = world_to_clip;
    
    // Reset accumulation if the camera position has changed.
    if (reset) {
        this->iterations = 0;
    }

    int accumulated_frames = this->iterations / (settings->ray_rate * settings->ray_rate);

    // Calculate which pixel we are tracing.
    int offset = this->iterations % (settings->ray_rate * settings->ray_rate) + settings->ray_rate * ((settings->ray_rate - 1) / 2) + ((settings->ray_rate - 1) / 2);
    glm::ivec2 pixel_offset(offset % settings->ray_rate, (offset / settings->ray_rate) % settings->ray_rate);

    // Offset visibility buffer for rendering at lower resolutions.
    glm::ivec2 visibility_buffer_size = (glm::ivec2(this->width, this->height) + settings->ray_rate - 1) / settings->ray_rate;
    glm::vec2 offset_translation = (settings->ray_rate - 1.0f - 2.0f * glm::vec2(pixel_offset)) / (float)settings->ray_rate;
    offset_translation.y = -offset_translation.y; 
    offset_translation /= glm::vec2(visibility_buffer_size);
    world_to_clip = glm::translate(glm::identity<glm::mat4x4>(), glm::vec3(offset_translation, 0.0f)) * world_to_clip;

    // Apply jitter.
    if (settings->jitter_matrix) {
        glm::vec2 jitter = (HaltonSequence((accumulated_frames % 256) + 1) * 2.0f - 1.0f) / glm::vec2(this->width, this->height);
        world_to_clip = glm::translate(glm::identity<glm::mat4x4>(), glm::vec3(jitter, 0.0f)) * world_to_clip;
    }

	glm::mat4x4 view_to_world = glm::affineInverse(world_to_view);
	glm::mat4x4 clip_to_world = glm::inverse(world_to_clip);
	glm::vec3 camera_pos = view_to_world[3];

	if (accumulated_frames < settings->max_accumulated_frames) {
        
        // Update the acceleration structure.
        context->BeginEvent("Acceleration Structure");
        context->BeginEvent("BLAS");
		BuildAllBlas(context, execute_params->gltf, &this->acceleration_structure);
		UpdateAllBlas(context, execute_params->gltf, &this->acceleration_structure);
        context->EndEvent();
        context->BeginEvent("TLAS");
		BuildTlas(context, execute_params->gltf, &this->acceleration_structure);
        context->EndEvent();
        context->EndEvent();

        // Rasterize camera rays.
        context->BeginEvent("V Buffer");
        context->PushTransitionBarrier(this->v_buffer_instance.Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        context->PushTransitionBarrier(this->v_buffer_primitive.Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        context->SubmitBarriers();
        
        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, (this->width + settings->ray_rate - 1) / settings->ray_rate, (this->height + settings->ray_rate - 1) / settings->ray_rate);
        context->SetViewports(1, &viewport);
        CD3DX12_RECT scissor_rect(0, 0, (this->width + settings->ray_rate - 1) / settings->ray_rate, (this->height + settings->ray_rate - 1) / settings->ray_rate);
        context->SetScissorRects(1, &scissor_rect);
        
        D3D12_CPU_DESCRIPTOR_HANDLE render_targets[] = {
            this->v_buffer_instance.Rtv(),
            this->v_buffer_primitive.Rtv(),
        };
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = this->v_buffer_depth.Dsv();
        context->SetRenderTargets(std::size(render_targets), render_targets, &dsv);
        context->ClearRenderTargetView(this->v_buffer_instance.Rtv(), this->v_buffer_instance.ClearColor());
        context->ClearRenderTargetView(this->v_buffer_primitive.Rtv(), this->v_buffer_primitive.ClearColor());
        context->ClearDepthStencilView(this->v_buffer_depth.Dsv(), this->v_buffer_depth.ClearDepth());
        context->SetGraphicsRootSignature(this->resources->GenericGraphicsRootSignature());
        context->SetPipelineState(this->v_buffer_pipeline.Get());
        context->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        struct {
            glm::mat4x4 world_to_clip;
            glm::vec2 jitter;
        } cb_per_frame = {
            .world_to_clip = world_to_clip,
        };
        context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME, context->CreateConstantBuffer(&cb_per_frame));
        
        for (const Vertices& vertices : vertex_buffers) {
            struct {
                glm::mat4x4 model_to_world;
            } cb_vertex;
            cb_vertex.model_to_world = mesh_instances[vertices.instance_id].transform;
            context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_DRAW, context->CreateConstantBuffer(&cb_vertex));
            struct {
                uint32_t instance_id;
            } cb_pixel;
            cb_pixel.instance_id = vertices.instance_id;
            context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_DRAW, context->CreateConstantBuffer(&cb_pixel));
            context->SetVertexBuffers(0, 1, &vertices.vertices);
            if (vertices.index_count > 0) {
                context->SetIndexBuffer(&vertices.index);
                context->DrawIndexedInstanced(vertices.index_count, 1, 0, 0, 0);
            } else {
                context->DrawInstanced(vertices.vertex_count, 1, 0, 0);
            }
        }
        context->EndEvent();

        // Alpha tested geometry.
        context->BeginEvent("V Buffer Alpha Tested");
        context->SetPipelineState(this->v_buffer_alpha_tested_pipeline.Get());
        context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME, context->CreateConstantBuffer(&cb_per_frame));
        for (const AlphaVertices& alpha_vertex: alpha_vertex_buffers) {
            struct {
                glm::mat4x4 model_to_world;
            } cb_vertex;
            cb_vertex.model_to_world = mesh_instances[alpha_vertex.instance_id].transform;
            context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_DRAW, context->CreateConstantBuffer(&cb_vertex));
            struct {
                uint32_t instance_id;
                uint32_t vertex_color;
                int materials_descriptor;
                int material_id;
            } cb_pixel;
            cb_pixel.instance_id = alpha_vertex.instance_id;
            cb_pixel.vertex_color = alpha_vertex.color.BufferLocation != 0 ? 1 : 0;
            cb_pixel.materials_descriptor = execute_params->gpu_scene->MaterialBuffer().Srv();
            cb_pixel.material_id = alpha_vertex.material_id;
            context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_DRAW, context->CreateConstantBuffer(&cb_pixel));
            D3D12_VERTEX_BUFFER_VIEW vertex_views[] = {
                alpha_vertex.vertices,
                alpha_vertex.tex_coords,
                alpha_vertex.color,
            };
            context->SetVertexBuffers(0, std::size(vertex_views), vertex_views);
            if (alpha_vertex.index_count > 0) {
                context->SetIndexBuffer(&alpha_vertex.index);
                context->DrawIndexedInstanced(alpha_vertex.index_count, 1, 0, 0, 0);
            } else {
                context->DrawInstanced(alpha_vertex.vertex_count, 1, 0, 0);
            }
        }
        context->EndEvent();

        // TODO: Do we need to set render targets to null before accessing them as an SRV in a shader?
        context->PushTransitionBarrier(this->v_buffer_instance.Resource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context->PushTransitionBarrier(this->v_buffer_primitive.Resource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        context->SubmitBarriers();

        // Shade camera rays and trace light and bounce rays.
        context->BeginEvent("Path Trace Scene");
        struct {
            glm::mat4x4 clip_to_world;
            glm::mat4x4 world_to_clip;
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
            int environment_alias_table_id;
            int environment_pdf;
            float luminance_clamp;
            float min_russian_roulette_continue_prob;
            float max_russian_roulette_continue_prob;
            float russian_roulette_active_lane_threshold;
            int v_buffer_primitive_id;
            int v_buffer_instance;
            int render_scale;
            int pixel_offset_x;
            int pixel_offset_y;
            int acceleration_structure_descriptor;
            int instances_descriptor;
            int materials_descriptor;
            int lights_descriptor;
        } constants;

        constants = {
            .clip_to_world = clip_to_world,
            .world_to_clip = world_to_clip,
            .camera_pos = camera_pos,
            .num_of_lights = execute_params->gpu_scene->LightCount(),
            .width = execute_params->width,
            .height = execute_params->height,
            .seed = settings->use_frame_as_seed ? (uint32_t)execute_params->frame : settings->seed,
            .accumulated_frames = accumulated_frames,
            .environment_color = settings->environment_color,
            .environment_intensity = settings->environment_intensity,
            .debug_output = settings->debug_output,
            .flags = this->iterations == 0 ? settings->flags | FLAG_FILL_ALL_PIXELS : settings->flags,
            .max_ray_length = 1000,
            .min_bounces = std::clamp(settings->min_bounces, 0, MAX_BOUNCES),
            .max_bounces = std::clamp(settings->max_bounces, 0, MAX_BOUNCES),
            .output_descriptor = execute_params->output->Uav(),
            .environment_map_descriptor_id = execute_params->environment_map ? execute_params->environment_map->cube.Srv() : -1,
            .environment_alias_table_id = execute_params->environment_map ? execute_params->environment_map->alias.Srv() : -1,
            .environment_pdf = execute_params->environment_map ? execute_params->environment_map->pdf.Srv() : -1,
            .luminance_clamp = settings->luminance_clamp,
            .min_russian_roulette_continue_prob = settings->min_russian_roulette_continue_prob,
            .max_russian_roulette_continue_prob = settings->max_russian_roulette_continue_prob,
            .russian_roulette_active_lane_threshold = settings->russian_roulette_active_lane_threshold,
            .v_buffer_primitive_id = this->v_buffer_primitive.Srv(),
            .v_buffer_instance = this->v_buffer_instance.Srv(),
            .render_scale = settings->ray_rate,
            .pixel_offset_x = pixel_offset.x,
            .pixel_offset_y = pixel_offset.y,
            .acceleration_structure_descriptor = this->acceleration_structure.GetAccelerationStructure().Srv(),
            .instances_descriptor = this->gpu_mesh_instances.Current().Srv(),
            .materials_descriptor = execute_params->gpu_scene->MaterialBuffer().Srv(),
            .lights_descriptor = execute_params->gpu_scene->LightBuffer().Srv(),
        };

        D3D12_GPU_VIRTUAL_ADDRESS constant_buffer = context->CreateConstantBuffer(&constants);

        context->SetComputeRootSignature(this->resources->GenericComputeRootSignature());
        context->SetComputeRootConstantBufferView(Gpu::GENERIC_COMPUTE_ROOT_PARAMETER_CONSTANT_BUFFER, constant_buffer);

        context->SetPipelineState(this->state_object.Get());

        D3D12_DISPATCH_RAYS_DESC desc = {
            .RayGenerationShaderRecord = this->shader_tables.ray_generation_shader_record,
            .MissShaderTable = this->shader_tables.miss_shader_table,
            .HitGroupTable = this->shader_tables.hit_group_table,
            .CallableShaderTable = this->shader_tables.callable_shader_table,
            .Width = (execute_params->width + settings->ray_rate - 1 - pixel_offset.x) / settings->ray_rate,
            .Height = (execute_params->height + settings->ray_rate - 1 - pixel_offset.y) / settings->ray_rate,
            .Depth = 1,
        };
        context->DispatchRays(&desc);

        if (settings->flags & FLAG_ACCUMULATE) {
            this->iterations++;
        } else {
            this->iterations = 0;
        }
		
        context->EndEvent();

        context->PushUavBarrier(execute_params->output->Resource());
		context->SubmitBarriers();
	}
}