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
	
	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob;
	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_error_blob;
	result = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, root_signature_blob.GetAddressOf(), root_signature_error_blob.GetAddressOf());
	assert(result == S_OK);
	result = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(this->root_signature.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = this->root_signature->SetName(L"Bloom Root Signature");
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
    result = mip_chain->SetName(L"Bloom Mip Chain");
    assert(result == S_OK);
}

void Bloom::Execute(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, DescriptorStack* transient_descriptors, ID3D12Resource* input, D3D12_RESOURCE_STATES input_resource_states, int iterations, float strength)
{
    iterations = std::min(this->max_iterations, iterations);

    command_list->SetComputeRootSignature(this->root_signature.Get());
    
    D3D12_RESOURCE_DESC input_desc = input->GetDesc();
    D3D12_RESOURCE_DESC mip_chain_desc = this->mip_chain->GetDesc();

    // Create descriptors.
	int descriptor_start = transient_descriptors->Allocate(iterations + 1);
	assert(descriptor_start != -1);
    const CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(input_desc.Format, 1, 0);
    const CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D(input_desc.Format, 0);
    transient_descriptors->CreateSrv(descriptor_start, input, &srv_desc);
    transient_descriptors->CreateUav(descriptor_start + 1, input, nullptr, &uav_desc);
	for (int i = 0; i < iterations; i++) {
		const CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(mip_chain_desc.Format, 1, i);
		const CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D(mip_chain_desc.Format, i);
		transient_descriptors->CreateSrv(descriptor_start + 2 * (i + 1), this->mip_chain.Get(), &srv_desc);
		transient_descriptors->CreateUav(descriptor_start + 2 * (i + 1) + 1, this->mip_chain.Get(), nullptr, &uav_desc);
	}

    // Downsample and blur.
    command_list->SetPipelineState(this->downsample_pipeline_state.Get());
    uint32_t width = input_desc.Width;
	uint32_t height = input_desc.Height;

    // First iteration using input as source texture.
    width = NextMipSize(width);
    height = NextMipSize(height);

    struct {
        int input_descriptor;
        int output_descriptor;
    } constant_buffer;

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(mip_chain.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);
    command_list->ResourceBarrier(1, &barrier);

    constant_buffer.input_descriptor = descriptor_start;
    constant_buffer.output_descriptor = descriptor_start + 3;

    command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
	command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

    CD3DX12_RESOURCE_BARRIER barriers[] = {
        CD3DX12_RESOURCE_BARRIER::UAV(mip_chain.Get()),
        CD3DX12_RESOURCE_BARRIER::Transition(mip_chain.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, 0),
    };
    command_list->ResourceBarrier(std::size(barriers), barriers);

    // Other iterations using mip chain texture.
    for (int i = 1; i < iterations; i++) {
        width = NextMipSize(width);
        height = NextMipSize(height);

		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(mip_chain.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i);
		command_list->ResourceBarrier(1, &barrier);

        constant_buffer.input_descriptor = descriptor_start + i * 2;
        constant_buffer.output_descriptor = descriptor_start + (i + 1) * 2 + 1;

		command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

		CD3DX12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(mip_chain.Get()),
			CD3DX12_RESOURCE_BARRIER::Transition(mip_chain.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i),
		};
		command_list->ResourceBarrier(std::size(barriers), barriers);
    }

    struct {
        int input_descriptor;
        int output_descriptor;
        float input_scale;
        float output_scale;
    } upsample_constant_buffer;

    // Upsample and reconstruct.
    command_list->SetPipelineState(this->upsample_pipeline_state.Get());
    for (int i = iterations - 1; i > 0; i--) {
		width = MipSize(input_desc.Width, i);
		height = MipSize(input_desc.Height, i);

		upsample_constant_buffer.input_descriptor = descriptor_start + (i + 1) * 2;
		upsample_constant_buffer.output_descriptor = descriptor_start + i * 2 + 1;
		upsample_constant_buffer.input_scale = 1.0f;
		upsample_constant_buffer.output_scale = 0.0f;

		// TODO: Consolidate these barriers into one.
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(mip_chain.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, i - 1);
		command_list->ResourceBarrier(1, &barrier);

		command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&upsample_constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
		command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);
		CD3DX12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::UAV(mip_chain.Get()),
			CD3DX12_RESOURCE_BARRIER::Transition(mip_chain.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, i - 1),
		};
		command_list->ResourceBarrier(std::size(barriers), barriers);
    }

    width = input_desc.Width;
    height = input_desc.Height;

    upsample_constant_buffer.input_descriptor = descriptor_start + 2;
    upsample_constant_buffer.output_descriptor = descriptor_start + 1;
    upsample_constant_buffer.input_scale = strength;
	upsample_constant_buffer.output_scale = 1.0f;

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(input, input_resource_states, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0);
    command_list->ResourceBarrier(1, &barrier);
    command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&upsample_constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
    command_list->Dispatch(CalculateThreadGroups(width, 8), CalculateThreadGroups(height, 8), 1);

}