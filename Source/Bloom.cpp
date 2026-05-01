#include "Bloom.h"

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>

#include "DirectXHelpers.h"
#include "GpuResources.h"

void Bloom::Create(Gpu::Resources* resources, uint16_t width, uint16_t height, uint8_t max_iterations)
{
    HRESULT result = S_OK;

    this->resources = resources;

    // Create the mip chain.
    Bloom::Resize(width, height, max_iterations);

    // Create the root signature.
	CD3DX12_ROOT_PARAMETER root_parameter;
	root_parameter.InitAsConstantBufferView(0);
	CD3DX12_STATIC_SAMPLER_DESC sampler_desc(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(1, &root_parameter, 1, &sampler_desc, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
	result = resources->CreateRootSignature(&root_signature_desc, &this->root_signature, "Bloom Root Signature");
	assert(result == S_OK);
    
    // Create the pipeline states.
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
		.pRootSignature = root_signature.Get(),
	};

    pipeline_desc.CS = Gpu::Resources::LoadShader("Shaders/BloomDownsample.cs.bin");
	result = resources->CreateComputePipelineState(&pipeline_desc, &this->downsample_pipeline_state, "Bloom Downsample");
	assert(result == S_OK);
	Gpu::Resources::FreeShader(pipeline_desc.CS);
    	
    pipeline_desc.CS = Gpu::Resources::LoadShader("Shaders/BloomUpsample.cs.bin");
	result = resources->CreateComputePipelineState(&pipeline_desc, &this->upsample_pipeline_state, "Bloom Upsample");
	assert(result == S_OK);
	Gpu::Resources::FreeShader(pipeline_desc.CS);
}

void Bloom::Resize(uint16_t width, uint16_t height, uint8_t max_iterations)
{
    // Recreate the mip chain.
    width = NextMipSize(width);
    height = NextMipSize(height);
    this->max_iterations = std::min(MipCount(width, height), (uint16_t)max_iterations);

    Gpu::TextureDesc desc = {
        .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .width = width,
        .height = height,
        .mip_levels = this->max_iterations,
        .flags = (Gpu::TextureFlags)(Gpu::TEXTURE_FLAG_SRV | Gpu::TEXTURE_FLAG_UAV | Gpu::TEXTURE_FLAG_SRV_PER_MIP),
        .initial_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        .name = "Bloom Mip Chain"
    };
    HRESULT result = resources->CreateTexture(&desc, &this->mip_chain);
    assert(result == S_OK);
}

void Bloom::Execute(CommandContext* context, Gpu::Texture* input, D3D12_RESOURCE_STATES input_resource_states, uint8_t iterations, float strength)
{
    iterations = std::min(this->max_iterations, iterations);

    context->command_list->SetComputeRootSignature(this->root_signature.Get());

    // Downsample and blur.
    context->command_list->SetPipelineState(this->downsample_pipeline_state.Get());
    uint16_t width = input->Width();
	uint16_t height = input->Height();

    // First iteration using input as source texture.
    width = NextMipSize(width);
    height = NextMipSize(height);

    struct {
        int input_descriptor;
        int output_descriptor;
    } constant_buffer;

    context->PushTransitionBarrier(mip_chain.Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);
    context->SubmitBarriers();

    constant_buffer.input_descriptor = input->Srv();
    constant_buffer.output_descriptor = mip_chain.Uav();

    context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&constant_buffer));
	context->command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

    context->PushUavBarrier(mip_chain.Resource());
    context->PushTransitionBarrier(mip_chain.Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);

    // Other iterations using mip chain texture.
    for (int i = 1; i < iterations; i++) {
        width = NextMipSize(width);
        height = NextMipSize(height);

		context->PushTransitionBarrier(mip_chain.Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i);
		context->SubmitBarriers();

        constant_buffer.input_descriptor = mip_chain.Srv(i - 1);
        constant_buffer.output_descriptor = mip_chain.Uav(i);

		context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&constant_buffer));
		context->command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

		context->PushUavBarrier(mip_chain.Resource());
		context->PushTransitionBarrier(mip_chain.Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i);
    }

    struct {
        int input_descriptor;
        int output_descriptor;
        float input_scale;
        float output_scale;
    } upsample_constant_buffer;

    // Upsample and reconstruct.
    context->command_list->SetPipelineState(this->upsample_pipeline_state.Get());
    for (int i = iterations - 1; i > 0; i--) {
		width = MipSize(input->Width(), i);
		height = MipSize(input->Height(), i);

		upsample_constant_buffer.input_descriptor = mip_chain.Srv(i);
		upsample_constant_buffer.output_descriptor = mip_chain.Uav(i - 1);
		upsample_constant_buffer.input_scale = 1.0f;
		upsample_constant_buffer.output_scale = 0.0f;

		context->PushTransitionBarrier(mip_chain.Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i - 1);
		context->SubmitBarriers();

		context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&upsample_constant_buffer));
		context->command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

		context->PushUavBarrier(mip_chain.Resource());
		context->PushTransitionBarrier(mip_chain.Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i - 1);
    }

    width = input->Width();
    height = input->Height();

    upsample_constant_buffer.input_descriptor = mip_chain.Srv(0);
    upsample_constant_buffer.output_descriptor = input->Uav();
    upsample_constant_buffer.input_scale = strength;
	upsample_constant_buffer.output_scale = 1.0f;

    context->PushTransitionBarrier(input->Resource(), input_resource_states, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);
    context->SubmitBarriers();
    context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&upsample_constant_buffer));
    context->command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);
}