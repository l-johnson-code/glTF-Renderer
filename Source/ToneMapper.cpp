#include "ToneMapper.h"

#include <cassert>

#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>
#include <glm/glm.hpp>

#include "GpuResources.h"

void ToneMapper::Create(Gpu::Resources* resources)
{
	HRESULT result;

	this->resources = resources;

    // Create pipeline.
	D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"SV_VERTEXID", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	Gpu::GraphicsPipelineDesc pipeline_desc = {
		.name = "Tone Mapper",
		.vertex_shader = "FullscreenTriangle",
		.pixel_shader = "ToneMapper",
		.rasterizer_state = {
			.FillMode = D3D12_FILL_MODE_SOLID,
			.CullMode = D3D12_CULL_MODE_BACK,
			.FrontCounterClockwise = TRUE,
			.DepthClipEnable = TRUE,
			.MultisampleEnable = FALSE,
			.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
		},
		.depth_stencil_state = {
			.DepthEnable = FALSE,
		},
		.input_layout = {
			.pInputElementDescs = input_layout,
			.NumElements = std::size(input_layout),
		},
		.primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.render_target_count = 1,
		.render_target_formats = {
			DXGI_FORMAT_R8G8B8A8_UNORM
		},
		.depth_stencil_format = DXGI_FORMAT_D32_FLOAT,
	};
	result = resources->CreateGraphicsPipelineState(&pipeline_desc, &this->pipeline_state);
	assert(result == S_OK);
}

void ToneMapper::Run(CommandContext* context, int input_descriptor, const Config* config)
{
	struct {
		int tonemapper;
		float exposure;
		int frame;
		int input_descriptor;
	} constant_buffer;

	constant_buffer = {
		.tonemapper = config->tonemapper,
		.exposure = config->exposure,
		.frame = config->frame,
		.input_descriptor = input_descriptor
	};
	D3D12_GPU_VIRTUAL_ADDRESS constant_buffer_gpu = context->CreateConstantBuffer(&constant_buffer);

	context->SetPipelineState(this->pipeline_state.Get());
	context->SetGraphicsRootSignature(this->resources->GenericGraphicsRootSignature());
	context->SetGraphicsRootConstantBufferView(Gpu::GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_DRAW, constant_buffer_gpu);
	context->DrawInstanced(3, 1, 0, 0);
}