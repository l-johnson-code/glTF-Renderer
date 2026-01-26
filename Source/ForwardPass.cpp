#include "ForwardPass.h"

#include <cassert>

#include <directx/d3d12.h>
#include <directx/d3dcommon.h>
#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>
#include <directx/dxgiformat.h>
#include <glm/gtc/matrix_inverse.hpp>

#include "GpuResources.h"

void ForwardPass::Create(ID3D12Device* device)
{
    HRESULT result;

	// Create the root signature.
	CD3DX12_ROOT_PARAMETER root_parameters[ROOT_PARAMETER_COUNT] = {};
	root_parameters[ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	root_parameters[ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_MODEL].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	root_parameters[ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_FRAME].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	root_parameters[ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_MODEL].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	root_parameters[ROOT_PARAMETER_SRV_LIGHTS].InitAsShaderResourceView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	root_parameters[ROOT_PARAMETER_SRV_MATERIALS].InitAsShaderResourceView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	CD3DX12_STATIC_SAMPLER_DESC static_samplers[] = {
		CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
		CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP)
	};
	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(ROOT_PARAMETER_COUNT, root_parameters, std::size(static_samplers), static_samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);

	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob;
	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_error_blob;
	result = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, root_signature_blob.GetAddressOf(), root_signature_error_blob.GetAddressOf());
	assert(result == S_OK);
	result = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(this->root_signature.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = this->root_signature->SetName(L"Forward Signature");
	assert(result == S_OK);

	// Load shaders.
	D3D12_SHADER_BYTECODE vertex_shader = GpuResources::LoadShader("Shaders/Forward.vs.bin");
	D3D12_SHADER_BYTECODE pixel_shader = GpuResources::LoadShader("Shaders/Forward.ps.bin");

	for (uint32_t permutation = 0; permutation < std::size(pipeline_states); permutation++) {
		CreatePipeline(device, vertex_shader, pixel_shader, permutation, root_signature.Get());
	}

	GpuResources::FreeShader(vertex_shader);
	GpuResources::FreeShader(pixel_shader);

	CreateBackgroundRenderer(device);
	CreateTranmissionMipPipeline(device);
}

void ForwardPass::CreatePipeline(ID3D12Device* device, D3D12_SHADER_BYTECODE vertex_shader, D3D12_SHADER_BYTECODE pixel_shader, uint32_t flags, ID3D12RootSignature* root_signature)
{
	HRESULT result;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};

	// Shaders.
	pipeline_desc.VS = vertex_shader;
	pipeline_desc.PS = pixel_shader;

	// Blend state.
	pipeline_desc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
	if (flags & PIPELINE_FLAGS_ALPHA_BLEND) {
		pipeline_desc.BlendState.RenderTarget[0] = {
			.BlendEnable = true,
			.SrcBlend = D3D12_BLEND_SRC_ALPHA,
			.DestBlend = D3D12_BLEND_INV_SRC_ALPHA,
			.BlendOp = D3D12_BLEND_OP_ADD,
			.SrcBlendAlpha = D3D12_BLEND_ONE,
			.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA,
			.BlendOpAlpha = D3D12_BLEND_OP_ADD,
			.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
		};
	}
	pipeline_desc.SampleMask = UINT_MAX;

	pipeline_desc.RasterizerState = {
		.FillMode = D3D12_FILL_MODE_SOLID,
		.CullMode = flags & PIPELINE_FLAGS_DOUBLE_SIDED ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK,
		.FrontCounterClockwise = flags & PIPELINE_FLAGS_WINDING_ORDER_CLOCKWISE ? FALSE : TRUE,
		.DepthClipEnable = TRUE,
		.MultisampleEnable = FALSE,
		.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
	};

	pipeline_desc.DepthStencilState = {
		.DepthEnable = TRUE,
		.DepthWriteMask = flags & PIPELINE_FLAGS_ALPHA_BLEND ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL,
		.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
		.StencilEnable = FALSE,
	};

    D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 4, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 5, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"PREVIOUS_POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 6, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
	pipeline_desc.InputLayout = {
		.pInputElementDescs = input_layout,
		.NumElements = std::size(input_layout),
	};
	pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// Render target formats.
	pipeline_desc.NumRenderTargets = 2;
	pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pipeline_desc.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
	pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pipeline_desc.SampleDesc.Count = 1;
	pipeline_desc.SampleDesc.Quality = 0;

	// Root signature.
	pipeline_desc.pRootSignature = root_signature;
	result = device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline_states[flags]));
	assert(result == S_OK);
	result = pipeline_states[flags]->SetName(L"Forward Pipeline");
	assert(result == S_OK);
}

void ForwardPass::Destroy()
{
	root_signature.Reset();
	for (auto& pipeline_state: pipeline_states) {
    	pipeline_state.Reset();
	}
}

void ForwardPass::SetRootSignature(ID3D12GraphicsCommandList* command_list)
{
	command_list->SetGraphicsRootSignature(this->root_signature.Get());
}

void ForwardPass::SetConfig(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, const Config* config)
{
	struct {
		alignas(16) glm::mat4x4 world_to_clip;
		alignas(16) glm::mat4x4 previous_world_to_clip;
	} cb_vertex;

	cb_vertex = {
		.world_to_clip = config->world_to_clip,
		.previous_world_to_clip = config->previous_world_to_clip,
	};

    struct {
		int width;
        int height;
        int num_of_lights;
		int ggx_cube_descriptor;
        alignas(16) glm::vec3 camera_pos;
		float environment_intensity;
		uint32_t render_flags;
		int diffuse_cube_descriptor;
		int transmission_descriptor;
	} cb_pixel;

	cb_pixel = {
		.width = config->width,
		.height = config->height,
		.num_of_lights = config->num_of_lights,
		.ggx_cube_descriptor = config->ggx_cube_descriptor,
		.camera_pos = config->camera_pos,
		.environment_intensity = config->environment_map_intensity,
		.render_flags = config->render_flags,
		.diffuse_cube_descriptor = config->diffuse_cube_descriptor,
		.transmission_descriptor = config->transmission_descriptor,
	};
	
	command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME, allocator->Copy(&cb_vertex, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
	command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_FRAME, allocator->Copy(&cb_pixel, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
	command_list->SetGraphicsRootShaderResourceView(ROOT_PARAMETER_SRV_LIGHTS, config->lights);
	command_list->SetGraphicsRootShaderResourceView(ROOT_PARAMETER_SRV_MATERIALS, config->materials);
}

void ForwardPass::BindRenderTargets(ID3D12GraphicsCommandList* command_list, GpuResources* resources, D3D12_CPU_DESCRIPTOR_HANDLE render, D3D12_CPU_DESCRIPTOR_HANDLE motion_vectors)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[] = {
        render,
		motion_vectors,
    };
	D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = resources->dsv_allocator.GetCpuHandle(GpuResources::DSV_DEPTH);
	command_list->OMSetRenderTargets(2, rtv_handles, false, &dsv_handle);
}

void ForwardPass::BindPipeline(ID3D12GraphicsCommandList* command_list, GpuResources* resources, uint32_t flags)
{
	assert(flags < std::size(pipeline_states));
	flags &= PIPELINE_FLAGS_BITMASK;
    command_list->SetPipelineState(pipeline_states[flags].Get());
}

void ForwardPass::Draw(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, Mesh* model, int material_id, glm::mat4x4 model_to_world, glm::mat4x4 model_to_world_normals, glm::mat4x4 previous_model_to_world, DynamicMesh* dynamic_mesh)
{
    // Write constant buffers.
	struct {
		alignas(16) glm::mat4x4 model_to_world;
		alignas(16) glm::mat4x4 model_to_world_normals;
		alignas(16) glm::mat4x4 previous_model_to_world;
	} vertex_per_model;

	vertex_per_model = {
		.model_to_world = model_to_world,
		.model_to_world_normals = glm::inverseTranspose(model_to_world),
		.previous_model_to_world = previous_model_to_world,
	};
	command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_MODEL, allocator->Copy(&vertex_per_model, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
	
	struct {
		uint32_t mesh_flags;
        int material_index;
        alignas(16) glm::mat4x4 model_to_world;
	} pixel_per_model;

	pixel_per_model = {
		.mesh_flags = model->flags,
		.material_index = material_id,
    	.model_to_world = model_to_world,
	};
	command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_MODEL, allocator->Copy(&pixel_per_model, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));

	if (model->topology != this->current_topology) {
		command_list->IASetPrimitiveTopology(model->topology);
		this->current_topology = model->topology;
	}
	
	// Set the vertex buffer.
	D3D12_VERTEX_BUFFER_VIEW vertex_buffers[] = {
		dynamic_mesh && (dynamic_mesh->flags & DynamicMesh::FLAG_POSITION) ? dynamic_mesh->GetCurrentPositionBuffer()->view : model->position.view, 
		dynamic_mesh && (dynamic_mesh->flags & DynamicMesh::FLAG_NORMAL) ? dynamic_mesh->normal.view : model->normal.view, 
		dynamic_mesh && (dynamic_mesh->flags & DynamicMesh::FLAG_TANGENT) ? dynamic_mesh->tangent.view : model->tangent.view, 
		model->texcoords[0].view,
		model->texcoords[1].view,
		model->color.view,
		// TODO: We don't always want to use the previous position buffer, such as on a new frame.
		dynamic_mesh && (dynamic_mesh->flags & DynamicMesh::FLAG_POSITION) ? dynamic_mesh->GetPreviousPositionBuffer()->view : model->position.view
	};
	command_list->IASetVertexBuffers(0, std::size(vertex_buffers), vertex_buffers);

    if (model->num_of_indices > 0) {
        command_list->IASetIndexBuffer(&model->index.view);
        command_list->DrawIndexedInstanced(model->num_of_indices, 1, 0, 0, 0);
    } else {
        command_list->DrawInstanced(model->num_of_vertices, 1, 0, 0);
    }
}

void ForwardPass::CreateBackgroundRenderer(ID3D12Device* device)
{
	HRESULT result;

	CD3DX12_ROOT_PARAMETER root_parameters[2];
	root_parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	root_parameters[1].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	CD3DX12_STATIC_SAMPLER_DESC static_samplers[] = {
		CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP)
	};
	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(
		std::size(root_parameters),
		root_parameters,
		std::size(static_samplers),
		static_samplers, 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
	);
	
	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob;
	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_error_blob;
	result = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, root_signature_blob.GetAddressOf(), root_signature_error_blob.GetAddressOf());
	assert(result == S_OK);
	result = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(this->background_root_signature.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = this->background_root_signature->SetName(L"Background Signature");
	assert(result == S_OK);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {};

	pipeline_desc.VS = GpuResources::LoadShader("Shaders/Background.vs.bin");
	pipeline_desc.PS = GpuResources::LoadShader("Shaders/Background.ps.bin");

	pipeline_desc.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT());
	pipeline_desc.SampleMask = UINT_MAX;

	pipeline_desc.RasterizerState = {
		.FillMode = D3D12_FILL_MODE_SOLID,
		.CullMode = D3D12_CULL_MODE_NONE,
		.FrontCounterClockwise = TRUE,
		.DepthClipEnable = TRUE,
	};

	pipeline_desc.DepthStencilState = {
		.DepthEnable = TRUE,
		.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
		.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
		.StencilEnable = FALSE,
	};

	// Create input layout.
 	D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"SV_VERTEXID", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	pipeline_desc.InputLayout.pInputElementDescs = input_layout;
	pipeline_desc.InputLayout.NumElements = std::size(input_layout);
	pipeline_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	pipeline_desc.NumRenderTargets = 1;
	pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	pipeline_desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
	pipeline_desc.SampleDesc.Count = 1;
	pipeline_desc.SampleDesc.Quality = 0;

	pipeline_desc.pRootSignature = this->background_root_signature.Get();

	result = device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(this->background_pipeline_state.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = background_pipeline_state->SetName(L"Background Pipeline");
	assert(result == S_OK);

	GpuResources::FreeShader(pipeline_desc.VS);
	GpuResources::FreeShader(pipeline_desc.PS);
}

void ForwardPass::DrawBackground(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, glm::mat4x4 clip_to_world, float environment_intensity, int environment_descriptor)
{
	command_list->SetGraphicsRootSignature(this->background_root_signature.Get());
    command_list->SetPipelineState(this->background_pipeline_state.Get());
	command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	struct {
		glm::mat4x4 clip_to_world;
	} cb_vertex;

	cb_vertex = {
		.clip_to_world = clip_to_world,
	};
	command_list->SetGraphicsRootConstantBufferView(0, allocator->Copy(&cb_vertex, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
	
	struct {
    	float environment_intensity;
    	int environment_descriptor;
	} cb_pixel;

	cb_pixel = {
		.environment_intensity = environment_intensity,
		.environment_descriptor = environment_descriptor,
	};
	command_list->SetGraphicsRootConstantBufferView(1, allocator->Copy(&cb_pixel, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));

    command_list->DrawInstanced(3, 1, 0, 0);
}

void ForwardPass::GenerateTransmissionMips(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, DescriptorStack* transient_descriptors, ID3D12Resource* input, ID3D12Resource* output)
{
	// Create mip 0.
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST, 0);
		command_list->ResourceBarrier(1, &barrier);
	}
	const CD3DX12_TEXTURE_COPY_LOCATION copy_dest(output);
	const CD3DX12_TEXTURE_COPY_LOCATION copy_source(input);
	command_list->CopyTextureRegion(&copy_dest, 0, 0, 0, &copy_source, nullptr);
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);
		command_list->ResourceBarrier(1, &barrier);
	}

	// Create descriptors.
	D3D12_RESOURCE_DESC output_desc = output->GetDesc();
	int descriptor_start = transient_descriptors->Allocate(output_desc.MipLevels * 2);
	assert(descriptor_start != -1);
	for (int i = 0; i < output_desc.MipLevels; i++) {
		const CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(output_desc.Format, 1, i);
		const CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D(output_desc.Format, i);
		transient_descriptors->CreateSrv(descriptor_start + 2 * i, output, &srv_desc);
		transient_descriptors->CreateUav(descriptor_start + 2 * i + 1, output, nullptr, &uav_desc);
	}

	// Generate the mips.
	command_list->SetComputeRootSignature(this->transmission_mips_root_signature.Get());
	command_list->SetPipelineState(this->transmission_mips_pipeline_state.Get());

	struct {
		int input_descriptor;
		int output_descriptor;
	} constant_buffer;

	uint32_t width = output_desc.Width;
	uint32_t height = output_desc.Height;

	for (int i = 1; i < output_desc.MipLevels; i++) {
		width = std::max(width / 2u, 1u);
		height = std::max(height / 2u, 1u);

		constant_buffer = {
			.input_descriptor = descriptor_start + (i - 1) * 2,
			.output_descriptor = descriptor_start + i * 2 + 1,
		};
		// TODO: Consolidate these barriers into one.
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i);
		command_list->ResourceBarrier(1, &barrier);

		command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		command_list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
		CD3DX12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(output),
			CD3DX12_RESOURCE_BARRIER::Transition(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i),
		};
		command_list->ResourceBarrier(std::size(barriers), barriers);
	}
}

void ForwardPass::CreateTranmissionMipPipeline(ID3D12Device* device)
{
	HRESULT result = S_OK;
	CD3DX12_ROOT_PARAMETER root_parameter;
	root_parameter.InitAsConstantBufferView(0);
	CD3DX12_STATIC_SAMPLER_DESC sampler_desc(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(1, &root_parameter, 1, &sampler_desc, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
	
	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob;
	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_error_blob;
	result = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, root_signature_blob.GetAddressOf(), root_signature_error_blob.GetAddressOf());
	assert(result == S_OK);
	result = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(this->transmission_mips_root_signature.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = this->transmission_mips_root_signature->SetName(L"Transmission Mip Root Signature");
	assert(result == S_OK);

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
		.pRootSignature = transmission_mips_root_signature.Get(),
		.CS = GpuResources::LoadShader("Shaders/TransmissionDownsample.cs.bin"),
	};
	result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->transmission_mips_pipeline_state));
	assert(result == S_OK);
	GpuResources::FreeShader(pipeline_desc.CS);
}