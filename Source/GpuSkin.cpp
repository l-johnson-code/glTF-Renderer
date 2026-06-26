#include "GpuSkin.h"

#include <algorithm>
#include <cassert>

#include <directx/d3d12.h>
#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_root_signature.h>

#include "Config.h"
#include "GpuResources.h"

void GpuSkin::Create(Gpu::Resources* resources)
{
    HRESULT result = S_OK;

	this->resources = resources;

    // Create the pipeline.
	Gpu::ComputePipelineDesc pipeline_desc = {
		.name = "Skin",
		.compute_shader = "Skin",
	};
	result = resources->CreateComputePipelineState(&pipeline_desc, &this->pipeline_state);
	assert(result == S_OK);
}

void GpuSkin::Bind(CommandContext* context)
{
    context->SetPipelineState(this->pipeline_state.Get());
    context->SetComputeRootSignature(this->resources->GenericComputeRootSignature());
}

void GpuSkin::Run(CommandContext* context, Mesh* input, DynamicMesh* output, int num_of_morph_targets, MorphTarget** morph_targets, float* morph_weights)
{
	context->PushTransitionBarrier(
		output->buffer.Resource(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);
	context->SubmitBarriers();

	struct {
		uint32_t num_of_vertices;
		uint32_t input_mesh_flags;
		uint32_t output_mesh_flags;
		int input_position_and_tangent_space_descriptor;
		int skin_descriptor;
		int bones_descriptor;
		int output_position_and_tangent_space_descriptor;
		int num_of_morph_targets;
		struct {
			float weight;
			int position_descriptor;
			uint32_t morph_flags;
			uint32_t padding;
		} morph_targets[Config::MAX_SIMULTANEOUS_MORPH_TARGETS];
	} constant_buffer;

	constant_buffer = {
		.num_of_vertices = output->num_of_vertices,
		.input_mesh_flags = input->flags,
		.output_mesh_flags = output->flags,
		.input_position_and_tangent_space_descriptor = input->position_and_tangent_space.descriptor,
		.skin_descriptor = input->joint_weight.descriptor,
		.bones_descriptor = output->GetCurrentBoneDescriptor(),
		.output_position_and_tangent_space_descriptor = output->GetCurrentPositionAndTangentSpaceBuffer()->descriptor,
		.num_of_morph_targets = std::min(num_of_morph_targets, Config::MAX_SIMULTANEOUS_MORPH_TARGETS),
	};
	for (int i = 0; i < constant_buffer.num_of_morph_targets; i++) {
		constant_buffer.morph_targets[i] = {
			.weight = morph_weights[i],
			.position_descriptor = morph_targets[i]->position_and_tangent_space.descriptor,
			.morph_flags = morph_targets[i]->flags,
		};
	}

	// If no bones are supplied, ignore skinning.
	if (!(output->flags & DynamicMesh::FLAG_SKELETON)) {
		constant_buffer.input_mesh_flags &= !Mesh::FLAG_JOINT_WEIGHT;
	}

    context->SetComputeRootConstantBufferView(Gpu::GENERIC_COMPUTE_ROOT_PARAMETER_CONSTANT_BUFFER, context->CreateConstantBuffer(&constant_buffer));

    context->Dispatch((constant_buffer.num_of_vertices + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE, 1, 1);

	// Transition output from UAV to vertex buffer and shader resource view state.
	context->PushUavBarrier(output->buffer.Resource());
	context->PushTransitionBarrier(
		output->buffer.Resource(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
	);
	context->SubmitBarriers();
}