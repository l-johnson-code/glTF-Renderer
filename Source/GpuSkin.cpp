#include "GpuSkin.h"

#include <algorithm>
#include <cassert>

#include <directx/d3d12.h>
#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_root_signature.h>

#include "Config.h"
#include "GpuResources.h"

void GpuSkin::Create(ID3D12Device* device)
{
    HRESULT result = S_OK;

	// Create the root signature.
	CD3DX12_ROOT_PARAMETER root_parameters[ROOT_PARAMETER_COUNT] = {};
	
    root_parameters[ROOT_PARAMETER_CONSTANT_BUFFER].InitAsConstantBufferView(0);
	root_parameters[ROOT_PARAMETER_VERTEX_INPUT].InitAsShaderResourceView(0);
	root_parameters[ROOT_PARAMETER_NORMAL_INPUT].InitAsShaderResourceView(1);
	root_parameters[ROOT_PARAMETER_TANGENT_INPUT].InitAsShaderResourceView(2);
	root_parameters[ROOT_PARAMETER_SKIN].InitAsShaderResourceView(3);
    root_parameters[ROOT_PARAMETER_BONES].InitAsShaderResourceView(4);
    root_parameters[ROOT_PARAMETER_VERTEX_OUTPUT].InitAsUnorderedAccessView(0);
    root_parameters[ROOT_PARAMETER_NORMAL_OUTPUT].InitAsUnorderedAccessView(1);
    root_parameters[ROOT_PARAMETER_TANGENT_OUTPUT].InitAsUnorderedAccessView(2);

    D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {
		.NumParameters = ROOT_PARAMETER_COUNT,
		.pParameters = &root_parameters[0],
		.NumStaticSamplers = 0,
		.pStaticSamplers = nullptr,
		.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED,
	};

	result = GpuResources::CreateRootSignature(device, &root_signature_desc, &this->root_signature, "GPU skin Root Signature");
	assert(result == S_OK);

    // Create the pipeline.
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
		.pRootSignature = this->root_signature.Get(),
		.CS = GpuResources::LoadShader("Shaders/Skin.cs.bin"),
	};
	result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->pipeline_state));
	assert(result == S_OK);

    // Cleanup.
	GpuResources::FreeShader(pipeline_desc.CS);
}

void GpuSkin::Bind(CommandContext* context)
{
    context->command_list->SetPipelineState(this->pipeline_state.Get());
    context->command_list->SetComputeRootSignature(this->root_signature.Get());
}

void GpuSkin::Run(CommandContext* context, Mesh* input, DynamicMesh* output, D3D12_GPU_VIRTUAL_ADDRESS bones, int num_of_morph_targets, MorphTarget** morph_targets, float* morph_weights)
{
	context->PushTransitionBarrier(
		output->resource.Get(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);
	context->SubmitBarriers();

	struct {
		uint32_t num_of_vertices;
		uint32_t input_mesh_flags;
		uint32_t output_mesh_flags;
		int num_of_morph_targets;
		struct {
			float weight;
			int position_descriptor;
			int normal_descriptor;
			int tangent_descriptor;
		} morph_targets[Config::MAX_SIMULTANEOUS_MORPH_TARGETS];
	} constant_buffer = {};

	constant_buffer = {
		.num_of_vertices = output->num_of_vertices,
		.input_mesh_flags = input->flags,
		.output_mesh_flags = output->flags,
		.num_of_morph_targets = std::min(num_of_morph_targets, Config::MAX_SIMULTANEOUS_MORPH_TARGETS),
	};
	for (int i = 0; i < constant_buffer.num_of_morph_targets; i++) {
		constant_buffer.morph_targets[i] = {
			.weight = morph_weights[i],
			.position_descriptor = morph_targets[i]->position.descriptor,
			.normal_descriptor = morph_targets[i]->normal.descriptor,
			.tangent_descriptor = morph_targets[i]->tangent.descriptor,
		};
	}

	// If no bones are supplied, ignore skinning.
	if (bones == 0) {
		constant_buffer.input_mesh_flags &= !Mesh::FLAG_JOINT_WEIGHT;
	}

    context->command_list->SetComputeRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER, context->CreateConstantBuffer(&constant_buffer));

    context->command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_VERTEX_INPUT, input->position.view.BufferLocation);
    context->command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_NORMAL_INPUT, input->normal.view.BufferLocation);
    context->command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_TANGENT_INPUT, input->tangent.view.BufferLocation);
	
    context->command_list->SetComputeRootUnorderedAccessView(ROOT_PARAMETER_VERTEX_OUTPUT, output->GetCurrentPositionBuffer()->view.BufferLocation);
    context->command_list->SetComputeRootUnorderedAccessView(ROOT_PARAMETER_NORMAL_OUTPUT, output->normal.view.BufferLocation);
    context->command_list->SetComputeRootUnorderedAccessView(ROOT_PARAMETER_TANGENT_OUTPUT, output->tangent.view.BufferLocation);

    context->command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_SKIN, input->joint_weight.view.BufferLocation);
    context->command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_BONES, bones);

    context->command_list->Dispatch((constant_buffer.num_of_vertices + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE, 1, 1);

	// Transition output from UAV to vertex buffer and shader resource view state.
	context->PushUavBarrier(output->resource.Get());
	context->PushTransitionBarrier(
		output->resource.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
	);
	context->SubmitBarriers();
}