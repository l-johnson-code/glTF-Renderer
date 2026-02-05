#include "ForwardPass.h"

#include <cassert>

#include <directx/d3d12.h>
#include <directx/d3dcommon.h>
#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>
#include <directx/dxgiformat.h>
#include <glm/gtc/matrix_inverse.hpp>

#include "DirectXHelpers.h"
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
	result = GpuResources::CreateRootSignature(device, &root_signature_desc, &this->root_signature, "Forward Signature");
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
	SetName(pipeline_states[flags].Get(), "Forward Pipeline");
}

void ForwardPass::Destroy()
{
	root_signature.Reset();
	for (auto& pipeline_state: pipeline_states) {
    	pipeline_state.Reset();
	}
}

void ForwardPass::SetRootSignature(CommandContext* context)
{
	context->command_list->SetGraphicsRootSignature(this->root_signature.Get());
}

void ForwardPass::SetConfig(CommandContext* context, const Config* config)
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
	
	context->command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME, context->CreateConstantBuffer(&cb_vertex));
	context->command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_FRAME, context->CreateConstantBuffer(&cb_pixel));
	context->command_list->SetGraphicsRootShaderResourceView(ROOT_PARAMETER_SRV_LIGHTS, config->lights);
	context->command_list->SetGraphicsRootShaderResourceView(ROOT_PARAMETER_SRV_MATERIALS, config->materials);
}

void ForwardPass::BindRenderTargets(CommandContext* context, D3D12_CPU_DESCRIPTOR_HANDLE render, D3D12_CPU_DESCRIPTOR_HANDLE motion_vectors, D3D12_CPU_DESCRIPTOR_HANDLE depth)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[] = {
        render,
		motion_vectors,
    };
	context->command_list->OMSetRenderTargets(2, rtv_handles, false, &depth);
}

void ForwardPass::BindPipeline(CommandContext* context, uint32_t flags)
{
	assert(flags < std::size(pipeline_states));
	flags &= PIPELINE_FLAGS_BITMASK;
    context->command_list->SetPipelineState(pipeline_states[flags].Get());
}

void ForwardPass::Draw(CommandContext* context, Mesh* model, int material_id, glm::mat4x4 model_to_world, glm::mat4x4 model_to_world_normals, glm::mat4x4 previous_model_to_world, DynamicMesh* dynamic_mesh)
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
	context->command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_MODEL, context->CreateConstantBuffer(&vertex_per_model));
	
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
	context->command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_MODEL, context->CreateConstantBuffer(&pixel_per_model));

	if (model->topology != this->current_topology) {
		context->command_list->IASetPrimitiveTopology(model->topology);
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
	context->command_list->IASetVertexBuffers(0, std::size(vertex_buffers), vertex_buffers);

    if (model->num_of_indices > 0) {
        context->command_list->IASetIndexBuffer(&model->index.view);
        context->command_list->DrawIndexedInstanced(model->num_of_indices, 1, 0, 0, 0);
    } else {
        context->command_list->DrawInstanced(model->num_of_vertices, 1, 0, 0);
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
	result = GpuResources::CreateRootSignature(device, &root_signature_desc, &this->background_root_signature, "Background Signature");
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
	SetName(background_pipeline_state.Get(), "Background Pipeline");

	GpuResources::FreeShader(pipeline_desc.VS);
	GpuResources::FreeShader(pipeline_desc.PS);
}

void ForwardPass::DrawBackground(CommandContext* context, glm::mat4x4 clip_to_world, float environment_intensity, int environment_descriptor)
{
	context->command_list->SetGraphicsRootSignature(this->background_root_signature.Get());
    context->command_list->SetPipelineState(this->background_pipeline_state.Get());
	context->command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	struct {
		glm::mat4x4 clip_to_world;
	} cb_vertex;

	cb_vertex = {
		.clip_to_world = clip_to_world,
	};
	context->command_list->SetGraphicsRootConstantBufferView(0, context->CreateConstantBuffer(&cb_vertex));
	
	struct {
    	float environment_intensity;
    	int environment_descriptor;
	} cb_pixel;

	cb_pixel = {
		.environment_intensity = environment_intensity,
		.environment_descriptor = environment_descriptor,
	};
	context->command_list->SetGraphicsRootConstantBufferView(1, context->CreateConstantBuffer(&cb_pixel));

    context->command_list->DrawInstanced(3, 1, 0, 0);
}

void ForwardPass::GenerateTransmissionMips(CommandContext* context, ID3D12Resource* input, ID3D12Resource* output, int sample_pattern)
{
	// Create mip 0.
	context->PushTransitionBarrier(output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST, 0);
	context->SubmitBarriers();

	const CD3DX12_TEXTURE_COPY_LOCATION copy_dest(output);
	const CD3DX12_TEXTURE_COPY_LOCATION copy_source(input);
	context->command_list->CopyTextureRegion(&copy_dest, 0, 0, 0, &copy_source, nullptr);
	context->PushTransitionBarrier(output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);

	// Create descriptors.
	D3D12_RESOURCE_DESC output_desc = output->GetDesc();
	DescriptorSpan descriptors = context->AllocateDescriptors(output_desc.MipLevels * 2);
	assert(!descriptors.IsEmpty());
	for (int i = 0; i < output_desc.MipLevels; i++) {
		const CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(output_desc.Format, 1, i);
		const CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D(output_desc.Format, i);
		context->CreateSrv(descriptors[2 * i], output, &srv_desc);
		context->CreateUav(descriptors[2 * i + 1], output, nullptr, &uav_desc);
	}

	// Generate the mips.
	context->command_list->SetComputeRootSignature(this->transmission_mips_root_signature.Get());
	context->command_list->SetPipelineState(this->transmission_mips_pipeline_state.Get());

	struct {
		int input_descriptor;
		int output_descriptor;
		int sample_pattern;
	} constant_buffer;
	constant_buffer.sample_pattern = sample_pattern;

	uint32_t width = output_desc.Width;
	uint32_t height = output_desc.Height;

	for (int i = 1; i < output_desc.MipLevels; i++) {
		width = std::max(width / 2u, 1u);
		height = std::max(height / 2u, 1u);

		constant_buffer.input_descriptor = descriptors[(i - 1) * 2];
		constant_buffer.output_descriptor = descriptors[i * 2 + 1];

		context->PushTransitionBarrier(output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i);
		context->SubmitBarriers();

		context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&constant_buffer));
		context->command_list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
		context->PushUavBarrier(output);
		context->PushTransitionBarrier(output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i);
	}
	context->SubmitBarriers();
}

void ForwardPass::CreateTranmissionMipPipeline(ID3D12Device* device)
{
	HRESULT result = S_OK;
	CD3DX12_ROOT_PARAMETER root_parameter;
	root_parameter.InitAsConstantBufferView(0);
	CD3DX12_STATIC_SAMPLER_DESC sampler_desc(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(1, &root_parameter, 1, &sampler_desc, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
	result = GpuResources::CreateRootSignature(device, &root_signature_desc, &this->transmission_mips_root_signature, "Transmission Mip Root Signature");
	assert(result == S_OK);

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
		.pRootSignature = transmission_mips_root_signature.Get(),
		.CS = GpuResources::LoadShader("Shaders/TransmissionDownsample.cs.bin"),
	};
	result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->transmission_mips_pipeline_state));
	assert(result == S_OK);
	GpuResources::FreeShader(pipeline_desc.CS);
}