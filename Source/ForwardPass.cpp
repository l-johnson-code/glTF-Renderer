#include "ForwardPass.h"

#include <cassert>

#include <directx/d3d12.h>
#include <directx/d3dcommon.h>
#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>
#include <directx/dxgiformat.h>

void ForwardPass::Create(Gpu::Resources* resources)
{
    HRESULT result;

	this->resources = resources;

	for (uint32_t permutation = 0; permutation < std::size(pipeline_states); permutation++) {
		CreatePipeline(resources, permutation);
	}

	CreateBackgroundRenderer(resources);
	CreateTranmissionMipPipeline(resources);
}

void ForwardPass::CreatePipeline(Gpu::Resources* resources, uint32_t flags)
{
	HRESULT result;

	D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TANGENT_SPACE", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R16G16B16A16_UNORM, 2, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"PREVIOUS_POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 3, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	Gpu::GraphicsPipelineDesc pipeline_desc = {
		.name = "Forward",
		.vertex_shader = "Forward",
		.pixel_shader = "Forward",
		.rasterizer_state = {
			.FillMode = D3D12_FILL_MODE_SOLID,
			.CullMode = flags & PIPELINE_FLAGS_DOUBLE_SIDED ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK,
			.FrontCounterClockwise = flags & PIPELINE_FLAGS_WINDING_ORDER_CLOCKWISE ? FALSE : TRUE,
			.DepthClipEnable = TRUE,
			.MultisampleEnable = FALSE,
			.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
		},
		.depth_stencil_state = {
			.DepthEnable = TRUE,
			.DepthWriteMask = flags & PIPELINE_FLAGS_ALPHA_BLEND ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL,
			.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
		},
		.input_layout = {
			.pInputElementDescs = input_layout,
			.NumElements = std::size(input_layout),
		},
		.primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.render_target_count = 2,
		.render_target_formats = {
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R16G16_FLOAT,
		},
		.depth_stencil_format = DXGI_FORMAT_D32_FLOAT,
	};

	// Blend state.
	if (flags & PIPELINE_FLAGS_ALPHA_BLEND) {
		pipeline_desc.blend_state.RenderTarget[0] = {
			.BlendEnable = TRUE,
			.SrcBlend = D3D12_BLEND_SRC_ALPHA,
			.DestBlend = D3D12_BLEND_INV_SRC_ALPHA,
			.BlendOp = D3D12_BLEND_OP_ADD,
			.SrcBlendAlpha = D3D12_BLEND_ONE,
			.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA,
			.BlendOpAlpha = D3D12_BLEND_OP_ADD,
			.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
		};
	}

	result = resources->CreateGraphicsPipelineState(&pipeline_desc, &pipeline_states[flags]);
	assert(result == S_OK);
}

void ForwardPass::Destroy()
{
	for (auto& pipeline_state: pipeline_states) {
    	pipeline_state.Reset();
	}
}

void ForwardPass::SetRootSignature(CommandContext* context)
{
	context->SetGraphicsRootSignature(this->resources->GenericGraphicsRootSignature());
}

void ForwardPass::SetConfig(CommandContext* context, const Config* config)
{
	struct {
		glm::mat4x4 world_to_clip;
		glm::mat4x4 previous_world_to_clip;
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
        glm::vec3 camera_pos;
		float environment_intensity;
		uint32_t render_flags;
		int diffuse_cube_descriptor;
		int transmission_descriptor;
		int lights_descriptor;
		int materials_descriptor;
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
		.lights_descriptor = config->lights->Srv(),
		.materials_descriptor = config->materials->Srv(),
	};
	
	context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME, context->CreateConstantBuffer(&cb_vertex));
	context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_FRAME, context->CreateConstantBuffer(&cb_pixel));
}

void ForwardPass::BindRenderTargets(CommandContext* context, D3D12_CPU_DESCRIPTOR_HANDLE render, D3D12_CPU_DESCRIPTOR_HANDLE motion_vectors, D3D12_CPU_DESCRIPTOR_HANDLE depth)
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handles[] = {
        render,
		motion_vectors,
    };
	context->SetRenderTargets(2, rtv_handles, &depth);
}

void ForwardPass::BindPipeline(CommandContext* context, uint32_t flags)
{
	assert(flags < std::size(pipeline_states));
	flags &= PIPELINE_FLAGS_BITMASK;
    context->SetPipelineState(pipeline_states[flags].Get());
}

void ForwardPass::Draw(CommandContext* context, Mesh* model, int material_id, glm::mat4x4 model_to_world, glm::mat4x4 model_to_world_normals, glm::mat4x4 previous_model_to_world, DynamicMesh* dynamic_mesh)
{
    // Write constant buffers.
	struct {
		glm::mat4x4 model_to_world;
		glm::mat4x4 model_to_world_normals;
		glm::mat4x4 previous_model_to_world;
	} vertex_per_model;

	vertex_per_model = {
		.model_to_world = model_to_world,
		.model_to_world_normals = glm::inverseTranspose(model_to_world),
		.previous_model_to_world = previous_model_to_world,
	};
	context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_DRAW, context->CreateConstantBuffer(&vertex_per_model));
	
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
	context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_DRAW, context->CreateConstantBuffer(&pixel_per_model));

	if (model->topology != this->current_topology) {
		context->SetPrimitiveTopology(model->topology);
		this->current_topology = model->topology;
	}
	
	// Set the vertex buffer.
	D3D12_VERTEX_BUFFER_VIEW vertex_buffers[] = {
		dynamic_mesh && (dynamic_mesh->flags & DynamicMesh::FLAG_POSITION) ? dynamic_mesh->GetCurrentPositionAndTangentSpaceBuffer()->view : model->position_and_tangent_space.view, 
		model->texcoords.view,
		model->color.view,
		// TODO: We don't always want to use the previous position buffer, such as on a new frame.
		dynamic_mesh && (dynamic_mesh->flags & DynamicMesh::FLAG_POSITION) ? dynamic_mesh->GetCurrentPositionAndTangentSpaceBuffer()->view : model->position_and_tangent_space.view
	};
	context->SetVertexBuffers(0, std::size(vertex_buffers), vertex_buffers);

    if (model->num_of_indices > 0) {
        context->SetIndexBuffer(&model->index.view);
        context->DrawIndexedInstanced(model->num_of_indices, 1, 0, 0, 0);
    } else {
        context->DrawInstanced(model->num_of_vertices, 1, 0, 0);
    }
}

void ForwardPass::CreateBackgroundRenderer(Gpu::Resources* resources)
{
	HRESULT result;

	// Create input layout.
 	D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"SV_VERTEXID", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	Gpu::GraphicsPipelineDesc pipeline_desc = {
		.name = "Background",
		.vertex_shader = "Background",
		.pixel_shader = "Background",
		.rasterizer_state = {
			.FillMode = D3D12_FILL_MODE_SOLID,
			.CullMode = D3D12_CULL_MODE_NONE,
			.FrontCounterClockwise = TRUE,
			.DepthClipEnable = TRUE,
		},
		.depth_stencil_state = {
			.DepthEnable = TRUE,
			.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
			.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
		},
		.input_layout = {
			.pInputElementDescs = input_layout,
			.NumElements = std::size(input_layout),
		},
		.primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.render_target_count = 1,
		.render_target_formats = {
			DXGI_FORMAT_R16G16B16A16_FLOAT,
		},
		.depth_stencil_format = DXGI_FORMAT_D32_FLOAT,
	};

	result = resources->CreateGraphicsPipelineState(&pipeline_desc, &this->background_pipeline_state);
	assert(result == S_OK);
}

void ForwardPass::DrawBackground(CommandContext* context, glm::mat4x4 clip_to_world, float environment_intensity, int environment_descriptor)
{
	context->SetGraphicsRootSignature(this->resources->GenericGraphicsRootSignature());
    context->SetPipelineState(this->background_pipeline_state.Get());
	context->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	struct {
		glm::mat4x4 clip_to_world;
	} cb_vertex;

	cb_vertex = {
		.clip_to_world = clip_to_world,
	};
	context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_DRAW, context->CreateConstantBuffer(&cb_vertex));
	
	struct {
    	float environment_intensity;
    	int environment_descriptor;
	} cb_pixel;

	cb_pixel = {
		.environment_intensity = environment_intensity,
		.environment_descriptor = environment_descriptor,
	};
	context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_DRAW, context->CreateConstantBuffer(&cb_pixel));

    context->DrawInstanced(3, 1, 0, 0);
}

void ForwardPass::GenerateTransmissionMips(CommandContext* context, Gpu::Texture* input, Gpu::Texture* output, int sample_pattern)
{
	// Create mip 0.
	context->PushTransitionBarrier(output->Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST, 0);
	context->SubmitBarriers();

	const CD3DX12_TEXTURE_COPY_LOCATION copy_dest(output->Resource());
	const CD3DX12_TEXTURE_COPY_LOCATION copy_source(input->Resource());
	context->CopyTextureRegion(&copy_dest, &copy_source);
	context->PushTransitionBarrier(output->Resource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);

	// Generate the mips.
	context->SetComputeRootSignature(this->resources->GenericComputeRootSignature());
	context->SetPipelineState(this->transmission_mips_pipeline_state.Get());

	struct {
		int input_descriptor;
		int output_descriptor;
		int sample_pattern;
	} constant_buffer;
	constant_buffer.sample_pattern = sample_pattern;

	uint32_t width = output->Width();
	uint32_t height = output->Height();

	for (int i = 1; i < output->MipLevels(); i++) {
		width = std::max(width / 2u, 1u);
		height = std::max(height / 2u, 1u);

		constant_buffer.input_descriptor = output->Srv(i - 1);
		constant_buffer.output_descriptor = output->Uav(i);

		context->PushTransitionBarrier(output->Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i);
		context->SubmitBarriers();

		context->SetComputeRootConstantBufferView(Gpu::GENERIC_COMPUTE_ROOT_PARAMETER_CONSTANT_BUFFER, context->CreateConstantBuffer(&constant_buffer));
		context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
		context->PushUavBarrier(output->Resource());
		context->PushTransitionBarrier(output->Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i);
	}
	context->SubmitBarriers();
}

void ForwardPass::CreateTranmissionMipPipeline(Gpu::Resources* resources)
{
	HRESULT result = S_OK;

	Gpu::ComputePipelineDesc pipeline_desc = {
		.name = "Transmission Downsample",
		.compute_shader = "TransmissionDownsample",
	};
	result = resources->CreateComputePipelineState(&pipeline_desc, &this->transmission_mips_pipeline_state);
	assert(result == S_OK);
}