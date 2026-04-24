#include "DescriptorAllocator.h"

#include <bit>

void TargetPoolBase::Destroy()
{
    this->device.Reset();
    this->descriptor_heap.Reset();
}

int TargetPoolBase::Size()
{
    return std::popcount(free_slots);
}

int TargetPoolBase::Capacity()
{
    return 64;
}

void TargetPoolBase::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    if (descriptor.ptr == 0) {
        return;
    }
    assert(
        (descriptor.ptr >= start.ptr) &&
        (descriptor.ptr < start.ptr + 64 * stride) &&
        (((descriptor.ptr - start.ptr) % stride) == 0) &&
        "Descriptor does not belong to this pool."
    );
    int index = (descriptor.ptr - start.ptr) / stride;
    free_slots |= 1 << index;
}

HRESULT TargetPoolBase::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
        .Type = type,
        .NumDescriptors = 64,
    };
    HRESULT result = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap));
    if (FAILED(result)) {
        Destroy();
        return result;
    }

    this->device = device;
    this->free_slots = std::numeric_limits<uint64_t>::max();
    this->stride = device->GetDescriptorHandleIncrementSize(type);
    this->start = descriptor_heap->GetCPUDescriptorHandleForHeapStart();

    return S_OK;
}

D3D12_CPU_DESCRIPTOR_HANDLE TargetPoolBase::AllocateDescriptor()
{
    if (free_slots == 0) {
        return {0}; 
    }
    int index = std::countr_zero(free_slots);
    free_slots &= ~(1 << index);
    return {start.ptr + index * stride};
}

HRESULT RenderTargetViewPool::Create(ID3D12Device* device)
{
    return TargetPoolBase::Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetViewPool::CreateRenderTargetView(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = AllocateDescriptor();
    if (descriptor.ptr != 0) {
        device->CreateRenderTargetView(resource, desc, descriptor);
    }
    return descriptor;
}

HRESULT DepthStencilViewPool::Create(ID3D12Device* device)
{
    return TargetPoolBase::Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
}

D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilViewPool::CreateDepthStencilView(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = AllocateDescriptor();
    if (descriptor.ptr != 0) {
        device->CreateDepthStencilView(resource, desc, descriptor);
    }
    return descriptor;
}