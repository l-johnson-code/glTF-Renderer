#include "CommandContext.h"

#include <directx/d3dx12_barriers.h>

void CommandContext::Init(ID3D12GraphicsCommandList4* command_list, CbvSrvUavStack* transient_descriptors, CpuMappedLinearBuffer* transient_allocator, std::vector<D3D12_RESOURCE_BARRIER>* barriers)
{
    this->command_list = command_list;
    this->transient_descriptors = transient_descriptors;
    this->transient_allocator = transient_allocator;
    this->barriers = barriers;
}

void CommandContext::PushTransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before_state, D3D12_RESOURCE_STATES after_state, uint32_t subresource, D3D12_RESOURCE_BARRIER_FLAGS flags)
{
    this->barriers->push_back(CD3DX12_RESOURCE_BARRIER::Transition(resource, before_state, after_state, subresource, flags));
}

void CommandContext::PushUavBarrier(ID3D12Resource* resource)
{
    this->barriers->push_back(CD3DX12_RESOURCE_BARRIER::UAV(resource));
}

void CommandContext::SubmitBarriers()
{
    this->command_list->ResourceBarrier(barriers->size(), barriers->data());
    barriers->resize(0);
}

void* CommandContext::Allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS* gpu_address)
{
    return this->transient_allocator->Allocate(size, alignment, gpu_address);
}

D3D12_GPU_VIRTUAL_ADDRESS CommandContext::AllocateAndCopy(const void* data, uint64_t size, uint64_t alignment)
{
    return this->transient_allocator->Copy(data, size, alignment);
}

D3D12_GPU_VIRTUAL_ADDRESS CommandContext::CreateConstantBuffer(const void* constant_buffer, uint64_t size)
{
    return this->transient_allocator->Copy(constant_buffer, size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
}

DescriptorSpan CommandContext::AllocateDescriptors(int count)
{
    int descriptor_start = this->transient_descriptors->Allocate(count);
    if (descriptor_start == -1) {
        return DescriptorSpan();
    }
    return DescriptorSpan(descriptor_start, count);
}

void CommandContext::CreateSrv(int index, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc)
{
    transient_descriptors->CreateSrv(index, resource, srv_desc);
}

void CommandContext::CreateUav(int index, ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc)
{
    transient_descriptors->CreateUav(index, resource, counter_resource, uav_desc);
}
