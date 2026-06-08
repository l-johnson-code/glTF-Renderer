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
    
    result = resources->CreateRootSignature(&root_signature_desc, &this->root_signature, "Path Tracer Root Signature");
    assert(SUCCEEDED(result));

    // Root signature shared between all shaders.
    D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {
        .pGlobalRootSignature = this->root_signature.Get(),
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
    uint64_t shader_table_size = ShaderTableCollectionBuilder::CalculateRequiredSize(1, 1, 0);
    Gpu::BufferDesc shader_table_buffer_desc = {
        .size = shader_table_size,
        .name = "Shader Table",
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
    CreateVBufferPipeline();
    CreateVBufferAlphaTestedPipeline();

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
        .format = DXGI_FORMAT_R32_UINT,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .mip_levels = 1,
        .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
        .flags = Gpu::TEXTURE_FLAG_RENDER_TARGET | Gpu::TEXTURE_FLAG_SRV,
        .initial_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        .name = "V Buffer Primitive ID",
    };
    result = resources->CreateTexture(&primitive_id_desc, &this->v_buffer_primitive);
    assert(SUCCEEDED(result));

    Gpu::TextureDesc instance_desc = {
        .format = DXGI_FORMAT_R32_UINT,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .mip_levels = 1,
        .clear_color = {0.0f, 0.0f, 0.0f, 0.0f},
        .flags = Gpu::TEXTURE_FLAG_RENDER_TARGET | Gpu::TEXTURE_FLAG_SRV,
        .initial_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        .name = "V Buffer Instance",
    };
    result = resources->CreateTexture(&instance_desc, &this->v_buffer_instance);
    assert(SUCCEEDED(result));

    Gpu::TextureDesc depth_desc = {
        .format = DXGI_FORMAT_D32_FLOAT,
        .width = (uint16_t)width,
        .height = (uint16_t)height,
        .mip_levels = 1,
        .clear_depth = 0.0f,
        .flags = Gpu::TEXTURE_FLAG_DEPTH_TARGET,
        .initial_state = D3D12_RESOURCE_STATE_DEPTH_WRITE,
        .name = "V Buffer Depth",
    };
    result = resources->CreateTexture(&depth_desc, &this->v_buffer_depth);
    assert(SUCCEEDED(result));

    this->width = width;
    this->height = height;
}

void Pathtracer::Shutdown()
{
    root_signature.Reset();
    state_object.Reset();
    if (resources) {
        resources->FreeBuffer(&shader_tables_buffer);
    }
}

void Pathtracer::CreateVBufferPipeline()
{
    HRESULT result = S_OK;

	// Create the root signature.
	CD3DX12_ROOT_PARAMETER root_parameters[VISIBILITY_ROOT_PARAMETER_COUNT] = {};
	root_parameters[VISIBILITY_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	root_parameters[VISIBILITY_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_MODEL].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	root_parameters[VISIBILITY_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_MODEL].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(VISIBILITY_ROOT_PARAMETER_COUNT, root_parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
	result = this->resources->CreateRootSignature(&root_signature_desc, &this->v_buffer_root_signature, "Visibility Signature");
	assert(result == S_OK);

	// Load shaders.
	D3D12_SHADER_BYTECODE vertex_shader = Gpu::Resources::LoadShader("Shaders/Visibility.vs.bin");
	D3D12_SHADER_BYTECODE pixel_shader = Gpu::Resources::LoadShader("Shaders/Visibility.ps.bin");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};

	// Shaders.
	pipeline_desc.VS = vertex_shader;
	pipeline_desc.PS = pixel_shader;

	// Blend state.
	pipeline_desc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
	pipeline_desc.SampleMask = UINT_MAX;

	pipeline_desc.RasterizerState = {
		.FillMode = D3D12_FILL_MODE_SOLID,
		.CullMode = D3D12_CULL_MODE_NONE,
		.FrontCounterClockwise = TRUE,
		.DepthClipEnable = TRUE,
		.MultisampleEnable = FALSE,
		.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
	};

	pipeline_desc.DepthStencilState = {
		.DepthEnable = TRUE,
		.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
		.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
		.StencilEnable = FALSE,
	};

    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	pipeline_desc.InputLayout = {
		.pInputElementDescs = input_layout,
		.NumElements = std::size(input_layout),
	};
	pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// Render target formats.
	pipeline_desc.NumRenderTargets = 2;
	pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
	pipeline_desc.RTVFormats[1] = DXGI_FORMAT_R32_UINT;
	pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pipeline_desc.SampleDesc.Count = 1;
	pipeline_desc.SampleDesc.Quality = 0;

	// Root signature.
	pipeline_desc.pRootSignature = this->v_buffer_root_signature.Get();
	result = this->resources->CreateGraphicsPipelineState(&pipeline_desc, &this->v_buffer_pipeline, "Visibility Pipeline");
	assert(result == S_OK);

    Gpu::Resources::FreeShader(vertex_shader);
	Gpu::Resources::FreeShader(pixel_shader);
}

void Pathtracer::CreateVBufferAlphaTestedPipeline()
{
    HRESULT result = S_OK;

	// Create the root signature.
	CD3DX12_ROOT_PARAMETER root_parameters[VISIBILITY_ALPHA_TESTED_ROOT_PARAMETER_COUNT] = {};
	root_parameters[VISIBILITY_ALPHA_TESTED_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	root_parameters[VISIBILITY_ALPHA_TESTED_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_MODEL].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	root_parameters[VISIBILITY_ALPHA_TESTED_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_MODEL].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	root_parameters[VISIBILITY_ALPHA_TESTED_ROOT_PARAMETER_MATERIALS].InitAsShaderResourceView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(VISIBILITY_ALPHA_TESTED_ROOT_PARAMETER_COUNT, root_parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
	result = this->resources->CreateRootSignature(&root_signature_desc, &this->v_buffer_alpha_tested_root_signature, "Visibility Alpha Tested Signature");
	assert(result == S_OK);

	// Load shaders.
	D3D12_SHADER_BYTECODE vertex_shader = Gpu::Resources::LoadShader("Shaders/VisibilityAlphaTested.vs.bin");
	D3D12_SHADER_BYTECODE pixel_shader = Gpu::Resources::LoadShader("Shaders/VisibilityAlphaTested.ps.bin");

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};

	// Shaders.
	pipeline_desc.VS = vertex_shader;
	pipeline_desc.PS = pixel_shader;

	// Blend state.
	pipeline_desc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
	pipeline_desc.SampleMask = UINT_MAX;

	pipeline_desc.RasterizerState = {
		.FillMode = D3D12_FILL_MODE_SOLID,
		.CullMode = D3D12_CULL_MODE_NONE,
		.FrontCounterClockwise = TRUE,
		.DepthClipEnable = TRUE,
		.MultisampleEnable = FALSE,
		.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
	};

	pipeline_desc.DepthStencilState = {
		.DepthEnable = TRUE,
		.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
		.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
		.StencilEnable = FALSE,
	};

    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R16G16B16A16_UNORM, 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	pipeline_desc.InputLayout = {
		.pInputElementDescs = input_layout,
		.NumElements = std::size(input_layout),
	};
	pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// Render target formats.
	pipeline_desc.NumRenderTargets = 2;
	pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R32_UINT;
	pipeline_desc.RTVFormats[1] = DXGI_FORMAT_R32_UINT;
	pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pipeline_desc.SampleDesc.Count = 1;
	pipeline_desc.SampleDesc.Quality = 0;

	// Root signature.
	pipeline_desc.pRootSignature = this->v_buffer_alpha_tested_root_signature.Get();
	result = this->resources->CreateGraphicsPipelineState(&pipeline_desc, &this->v_buffer_alpha_tested_pipeline, "Visibility Alpha Tested Pipeline");
	assert(result == S_OK);

    Gpu::Resources::FreeShader(vertex_shader);
	Gpu::Resources::FreeShader(pixel_shader);
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

void Pathtracer::BuildTlas(CommandContext* context, Gltf* gltf, int scene_id, RaytracingAccelerationStructure* acceleration_structure)
{
	mesh_instances.clear();
    vertex_buffers.clear();
    alpha_vertex_buffers.clear();
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
					.position_and_tangent_space_descriptor = mesh.position_and_tangent_space.descriptor,
					.texcoord_descriptors = {
						mesh.texcoords[0].descriptor,
						mesh.texcoords[1].descriptor,
					},
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
                                    .tex_coords = { mesh.texcoords[0].view, mesh.texcoords[1].view },
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
                                .tex_coords = { mesh.texcoords[0].view, mesh.texcoords[1].view },
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
	});

    acceleration_structure->BuildTlas(context);
	this->gpu_mesh_instances = context->AllocateAndCopy(mesh_instances.data(), sizeof(GpuMeshInstance) * mesh_instances.size(), 4);
}

void Pathtracer::PathtraceScene(CommandContext* context, const Settings* settings, const ExecuteParams* execute_params)
{
    glm::mat4x4 world_to_view = execute_params->camera->GetWorldToView();
	glm::mat4x4 view_to_clip = execute_params->camera->GetViewToClip();
    glm::mat4x4 world_to_clip = view_to_clip * world_to_view;
    bool reset = (world_to_clip != previous_world_to_clip) || (settings->reset);
    previous_world_to_clip = world_to_clip;
    
    // Apply jitter.
    if (settings->jitter_matrix) {
        glm::vec2 jitter = (HaltonSequence((execute_params->frame % 256) + 1) * 2.0f - 1.0f) / glm::vec2(this->width, this->height);
        world_to_clip = glm::translate(glm::identity<glm::mat4x4>(), glm::vec3(jitter, 0.0f)) * world_to_clip;
    }

	glm::mat4x4 view_to_world = glm::affineInverse(world_to_view);
	glm::mat4x4 clip_to_world = glm::inverse(world_to_clip);
	glm::vec3 camera_pos = view_to_world[3];

    // Reset accumulation if the camera position has changed.
    if (reset) {
        this->accumulated_frames = 0;
    }

	if (accumulated_frames < settings->max_accumulated_frames) {
        
        // Update the acceleration structure.
        context->BeginEvent("Acceleration Structure");
        context->BeginEvent("BLAS");
		BuildAllBlas(context, execute_params->gltf, &this->acceleration_structure);
		UpdateAllBlas(context, execute_params->gltf, &this->acceleration_structure);
        context->EndEvent();
        context->BeginEvent("TLAS");
		BuildTlas(context, execute_params->gltf, execute_params->scene, &this->acceleration_structure);
        context->EndEvent();
        context->EndEvent();

        // Rasterize camera rays.
        context->BeginEvent("V Buffer");
        context->PushTransitionBarrier(this->v_buffer_instance.Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        context->PushTransitionBarrier(this->v_buffer_primitive.Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        context->SubmitBarriers();
        
        CD3DX12_VIEWPORT viewport(0.0f, 0.0f, this->width, this->height);
        context->SetViewports(1, &viewport);
        CD3DX12_RECT scissor_rect(0, 0, this->width, this->height);
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
        context->SetGraphicsRootSignature(this->v_buffer_root_signature.Get());
        context->SetPipelineState(this->v_buffer_pipeline.Get());
        context->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        struct {
            glm::mat4x4 world_to_clip;
            glm::vec2 jitter;
        } cb_per_frame = {
            .world_to_clip = world_to_clip,
        };
        context->SetGraphicsRootConstantBufferView(VISIBILITY_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME, context->CreateConstantBuffer(&cb_per_frame));
        
        for (const Vertices& vertices : vertex_buffers) {
            struct {
                glm::mat4x4 model_to_world;
            } cb_vertex;
            cb_vertex.model_to_world = mesh_instances[vertices.instance_id].transform;
            context->SetGraphicsRootConstantBufferView(VISIBILITY_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_MODEL, context->CreateConstantBuffer(&cb_vertex));
            struct {
                uint32_t instance_id;
            } cb_pixel;
            cb_pixel.instance_id = vertices.instance_id;
            context->SetGraphicsRootConstantBufferView(VISIBILITY_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_MODEL, context->CreateConstantBuffer(&cb_pixel));
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
        context->SetGraphicsRootSignature(this->v_buffer_alpha_tested_root_signature.Get());
        context->SetPipelineState(this->v_buffer_alpha_tested_pipeline.Get());
        context->SetGraphicsRootConstantBufferView(VISIBILITY_ALPHA_TESTED_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME, context->CreateConstantBuffer(&cb_per_frame));
        context->SetGraphicsRootShaderResourceView(VISIBILITY_ALPHA_TESTED_ROOT_PARAMETER_MATERIALS, execute_params->gpu_materials);
        for (const AlphaVertices& alpha_vertex: alpha_vertex_buffers) {
            struct {
                glm::mat4x4 model_to_world;
            } cb_vertex;
            cb_vertex.model_to_world = mesh_instances[alpha_vertex.instance_id].transform;
            context->SetGraphicsRootConstantBufferView(VISIBILITY_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_MODEL, context->CreateConstantBuffer(&cb_vertex));
            struct {
                uint32_t instance_id;
                uint32_t vertex_color;
                int material_id;
            } cb_pixel;
            cb_pixel.instance_id = alpha_vertex.instance_id;
            cb_pixel.vertex_color = alpha_vertex.color.BufferLocation != 0 ? 1 : 0;
            cb_pixel.material_id = alpha_vertex.material_id;
            context->SetGraphicsRootConstantBufferView(VISIBILITY_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_MODEL, context->CreateConstantBuffer(&cb_pixel));
            D3D12_VERTEX_BUFFER_VIEW vertex_views[] = {
                alpha_vertex.vertices,
                alpha_vertex.tex_coords[0],
                alpha_vertex.tex_coords[1],
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
        } constants;

        constants = {
            .clip_to_world = clip_to_world,
            .world_to_clip = world_to_clip,
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
        };

        D3D12_GPU_VIRTUAL_ADDRESS constant_buffer = context->CreateConstantBuffer(&constants);

        context->SetComputeRootSignature(this->root_signature.Get());
        context->SetComputeRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER, constant_buffer);
        context->SetComputeRootShaderResourceView(ROOT_PARAMETER_ACCELERATION_STRUCTURE, this->acceleration_structure.GetAccelerationStructure());
        context->SetComputeRootShaderResourceView(ROOT_PARAMETER_INSTANCES, this->gpu_mesh_instances);
        context->SetComputeRootShaderResourceView(ROOT_PARAMETER_MATERIALS, execute_params->gpu_materials);
        context->SetComputeRootShaderResourceView(ROOT_PARAMETER_LIGHTS, execute_params->gpu_lights);

        context->SetPipelineState(this->state_object.Get());

        D3D12_DISPATCH_RAYS_DESC desc = {
            .RayGenerationShaderRecord = this->shader_tables.ray_generation_shader_record,
            .MissShaderTable = this->shader_tables.miss_shader_table,
            .HitGroupTable = this->shader_tables.hit_group_table,
            .CallableShaderTable = this->shader_tables.callable_shader_table,
            .Width = execute_params->width,
            .Height = execute_params->height,
            .Depth = 1,
        };
        context->DispatchRays(&desc);

        if (settings->flags & FLAG_ACCUMULATE) {
            this->accumulated_frames++;
        } else {
            this->accumulated_frames = 0;
        }
		
        context->EndEvent();

        context->PushUavBarrier(execute_params->output->Resource());
		context->SubmitBarriers();
	}
}