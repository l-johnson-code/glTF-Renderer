#include "ToneMapper.h"

#include <cassert>

#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>
#include <glm/glm.hpp>

#include "DirectXHelpers.h"
#include "GpuResources.h"

void ToneMapper::Create(ID3D12Device* device)
{
	HRESULT result;

	// Create the root signature.
	CD3DX12_ROOT_PARAMETER root_parameters[ROOT_PARAMETER_COUNT];
	CD3DX12_DESCRIPTOR_RANGE descriptor_range(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0);
	root_parameters[ROOT_PARAMETER_INPUT].InitAsDescriptorTable(1, &descriptor_range, D3D12_SHADER_VISIBILITY_PIXEL);
	root_parameters[ROOT_PARAMETER_CONFIG].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(ROOT_PARAMETER_COUNT, root_parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	result = GpuResources::CreateRootSignature(device, &root_signature_desc, &this->root_signature, "Tone Mapper Root Signature");
	assert(result == S_OK);

    // Create pipeline.
	D3D12_INPUT_ELEMENT_DESC input_layout[] = {
		{"SV_VERTEXID", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc = {
		.pRootSignature = this->root_signature.Get(),
		.VS = GpuResources::LoadShader("Shaders/FullscreenTriangle.vs.bin"),
		.PS = GpuResources::LoadShader("Shaders/ToneMapper.ps.bin"),
		.BlendState = CD3DX12_BLEND_DESC(CD3DX12_DEFAULT()),
		.SampleMask = UINT_MAX,
		.RasterizerState = {
			.FillMode = D3D12_FILL_MODE_SOLID,
			.CullMode = D3D12_CULL_MODE_BACK,
			.FrontCounterClockwise = TRUE,
			.DepthClipEnable = TRUE,
			.MultisampleEnable = FALSE,
			.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
		},
		.DepthStencilState = {
			.DepthEnable = FALSE,
			.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
			.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS,
			.StencilEnable = FALSE,
		},
		.InputLayout = {
			.pInputElementDescs = input_layout,
			.NumElements = std::size(input_layout),
		},
		.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
		.NumRenderTargets = 1,
		.RTVFormats = { DXGI_FORMAT_R8G8B8A8_UNORM },
		.DSVFormat = DXGI_FORMAT_D32_FLOAT,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0,
		}
	};
	result = device->CreateGraphicsPipelineState(&pipeline_desc, IID_PPV_ARGS(&this->pipeline_state));
	assert(result == S_OK);
	SetName(this->pipeline_state.Get(), "Tone Mapper Pipeline");

    // Cleanup.
	GpuResources::FreeShader(pipeline_desc.VS);
	GpuResources::FreeShader(pipeline_desc.PS);
}

void ToneMapper::Run(CommandContext* context, D3D12_GPU_DESCRIPTOR_HANDLE input_descriptor, const Config* config)
{
	struct {
		int tonemapper;
		float exposure;
		glm::vec2 toe_end;
		glm::vec2 compression_start;
		int frame;
	} constant_buffer;

	constant_buffer = {
		.tonemapper = config->tonemapper,
		.exposure = config->exposure,
		.frame = config->frame,
	};
	D3D12_GPU_VIRTUAL_ADDRESS constant_buffer_gpu = context->CreateConstantBuffer(&constant_buffer);

	context->command_list->SetPipelineState(this->pipeline_state.Get());
	context->command_list->SetGraphicsRootSignature(this->root_signature.Get());
	context->command_list->SetGraphicsRootDescriptorTable(ROOT_PARAMETER_INPUT, input_descriptor);
	context->command_list->SetGraphicsRootConstantBufferView(ROOT_PARAMETER_CONFIG, constant_buffer_gpu);
	context->command_list->DrawInstanced(3, 1, 0, 0);
}