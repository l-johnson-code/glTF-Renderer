#include "Bloom.h"

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>

#include "DirectXHelpers.h"
#include "GpuResources.h"

void Bloom::Create(ID3D12Device* device, uint32_t width, uint32_t height, int max_iterations)
{
    HRESULT result = S_OK;

    this->device = device;

    // Create the mip chain.
    Bloom::Resize(width, height, max_iterations);

    // Create the root signature.
	CD3DX12_ROOT_PARAMETER root_parameter;
	root_parameter.InitAsConstantBufferView(0);
	CD3DX12_STATIC_SAMPLER_DESC sampler_desc(0, D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(1, &root_parameter, 1, &sampler_desc, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
	result = GpuResources::CreateRootSignature(device, &root_signature_desc, &this->root_signature, "Bloom Root Signature");
	assert(result == S_OK);
    
    // Create the pipeline states.
	D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {
		.pRootSignature = root_signature.Get(),
	};

    pipeline_desc.CS = GpuResources::LoadShader("Shaders/BloomDownsample.cs.bin");
	result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->downsample_pipeline_state));
	assert(result == S_OK);
	GpuResources::FreeShader(pipeline_desc.CS);
    	
    pipeline_desc.CS = GpuResources::LoadShader("Shaders/BloomUpsample.cs.bin");
	result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->upsample_pipeline_state));
	assert(result == S_OK);
	GpuResources::FreeShader(pipeline_desc.CS);
}

void Bloom::Resize(uint32_t width, uint32_t height, int max_iterations)
{
    // Recreate the mip chain.
    width = NextMipSize(width);
    height = NextMipSize(height);
    this->max_iterations = std::min(MipCount(width, height), (uint16_t)max_iterations);
    CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, this->max_iterations);
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&this->mip_chain));
    assert(result == S_OK);
    SetName(mip_chain.Get(), "Bloom Mip Chain");
}

void Bloom::Execute(CommandContext* context, ID3D12Resource* input, D3D12_RESOURCE_STATES input_resource_states, int iterations, float strength)
{
    iterations = std::min(this->max_iterations, iterations);

    context->command_list->SetComputeRootSignature(this->root_signature.Get());
    
    D3D12_RESOURCE_DESC input_desc = input->GetDesc();
    D3D12_RESOURCE_DESC mip_chain_desc = this->mip_chain->GetDesc();

    // Create descriptors.
	DescriptorSpan descriptors = context->AllocateDescriptors((iterations + 1) * 2);
	assert(!descriptors.IsEmpty());
    const CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(input_desc.Format, 1, 0);
    const CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D(input_desc.Format, 0);
    context->CreateSrv(descriptors[0], input, &srv_desc);
    context->CreateUav(descriptors[1], input, nullptr, &uav_desc);
	for (int i = 0; i < iterations; i++) {
		const CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(mip_chain_desc.Format, 1, i);
		const CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D(mip_chain_desc.Format, i);
		context->CreateSrv(descriptors[2 * (i + 1)], this->mip_chain.Get(), &srv_desc);
		context->CreateUav(descriptors[2 * (i + 1) + 1], this->mip_chain.Get(), nullptr, &uav_desc);
	}

    // Downsample and blur.
    context->command_list->SetPipelineState(this->downsample_pipeline_state.Get());
    uint32_t width = input_desc.Width;
	uint32_t height = input_desc.Height;

    // First iteration using input as source texture.
    width = NextMipSize(width);
    height = NextMipSize(height);

    struct {
        int input_descriptor;
        int output_descriptor;
    } constant_buffer;

    context->PushTransitionBarrier(mip_chain.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);
    context->SubmitBarriers();

    constant_buffer.input_descriptor = descriptors[0];
    constant_buffer.output_descriptor = descriptors[3];

    context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&constant_buffer));
	context->command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

    context->PushUavBarrier(mip_chain.Get());
    context->PushTransitionBarrier(mip_chain.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0);

    // Other iterations using mip chain texture.
    for (int i = 1; i < iterations; i++) {
        width = NextMipSize(width);
        height = NextMipSize(height);

		context->PushTransitionBarrier(mip_chain.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i);
		context->SubmitBarriers();

        constant_buffer.input_descriptor = descriptors[i * 2];
        constant_buffer.output_descriptor = descriptors[(i + 1) * 2 + 1];

		context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&constant_buffer));
		context->command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

		context->PushUavBarrier(mip_chain.Get());
		context->PushTransitionBarrier(mip_chain.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i);
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
		width = MipSize(input_desc.Width, i);
		height = MipSize(input_desc.Height, i);

		upsample_constant_buffer.input_descriptor = descriptors[(i + 1) * 2];
		upsample_constant_buffer.output_descriptor = descriptors[i * 2 + 1];
		upsample_constant_buffer.input_scale = 1.0f;
		upsample_constant_buffer.output_scale = 0.0f;

		context->PushTransitionBarrier(mip_chain.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i - 1);
		context->SubmitBarriers();

		context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&upsample_constant_buffer));
		context->command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

		context->PushUavBarrier(mip_chain.Get());
		context->PushTransitionBarrier(mip_chain.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i - 1);
    }

    width = input_desc.Width;
    height = input_desc.Height;

    upsample_constant_buffer.input_descriptor = descriptors[2];
    upsample_constant_buffer.output_descriptor = descriptors[1];
    upsample_constant_buffer.input_scale = strength;
	upsample_constant_buffer.output_scale = 1.0f;

    context->PushTransitionBarrier(input, input_resource_states, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);
    context->SubmitBarriers();
    context->command_list->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&upsample_constant_buffer));
    context->command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);
}