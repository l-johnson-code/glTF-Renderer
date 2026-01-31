#include "DescriptorAllocator.h"

#include <cassert>

#include <directx/d3d12.h>

void DescriptorRange::Create(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc)
{
    HRESULT result = S_OK;
	
    result = device->CreateDescriptorHeap(desc, IID_PPV_ARGS(this->descriptor_heap.ReleaseAndGetAddressOf()));
	assert(result == S_OK);

    this->device = device;
    this->descriptor_start = 0;
    this->capacity = desc->NumDescriptors;
    this->stride = this->device->GetDescriptorHandleIncrementSize(desc->Type);
    this->cpu_start = this->descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        this->gpu_start = this->descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    } else {
        this->gpu_start = {0};
    }
}

void DescriptorRange::Create(ID3D12DescriptorHeap* descriptor_heap, int start, int capacity)
{
    HRESULT result = S_OK;
	
    this->descriptor_heap = descriptor_heap;

    D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = descriptor_heap->GetDesc();
    assert(start >= 0);
    assert((start + capacity) <= descriptor_heap_desc.NumDescriptors);

    result = descriptor_heap->GetDevice(IID_PPV_ARGS(this->device.ReleaseAndGetAddressOf()));
	assert(result == S_OK);

    this->descriptor_start = start;
    this->capacity = capacity;
    this->stride = this->device->GetDescriptorHandleIncrementSize(descriptor_heap_desc.Type);
    this->cpu_start = this->descriptor_heap->GetCPUDescriptorHandleForHeapStart();
    if (descriptor_heap_desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
        this->gpu_start = this->descriptor_heap->GetGPUDescriptorHandleForHeapStart();
    } else {
        this->gpu_start = {0};
    }
}

void DescriptorRange::Create(const DescriptorRange* descriptor_range, int start, int capacity)
{
    this->device = descriptor_range->device;
    this->descriptor_heap = descriptor_range->descriptor_heap;
    this->descriptor_start = start;
    this->capacity = capacity;
    this->stride = descriptor_range->stride;
    this->cpu_start = descriptor_range->cpu_start;
    this->gpu_start = descriptor_range->gpu_start;
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorRange::GetCpuHandle(int index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE result = this->cpu_start;
    result.ptr += index * this->stride;
    return result;
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorRange::GetGpuHandle(int index) const
{
    assert(gpu_start.ptr != 0);
    D3D12_GPU_DESCRIPTOR_HANDLE result = this->gpu_start;
    result.ptr += index * this->stride;
    return result;
}

int DescriptorRange::GetIndex(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle) const
{
    assert(((cpu_handle.ptr - this->cpu_start.ptr) % this->stride) == 0);
    int index = ((cpu_handle.ptr - this->cpu_start.ptr) / this->stride);
    assert(index >= this->descriptor_start);
    assert(index < (this->descriptor_start + this->capacity));
    return index;
}

int DescriptorRange::GetIndex(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) const
{
    assert(((gpu_handle.ptr - this->gpu_start.ptr) % this->stride) == 0);
    int index = ((gpu_handle.ptr - this->gpu_start.ptr) / this->stride);
    assert(index >= this->descriptor_start);
    assert(index < (this->descriptor_start + this->capacity));
    return index;
}

int DescriptorRange::GetAbsoluteIndex(int relative_index) const
{
    return relative_index + this->descriptor_start;
}

int DescriptorRange::GetRelativeIndex(int absolute_index) const
{
    return absolute_index - this->descriptor_start;
}

int DescriptorRange::Capacity() const
{
    return this->capacity;
}

ID3D12DescriptorHeap* DescriptorRange::DescriptorHeap()
{
    return this->descriptor_heap.Get();
}

void DescriptorRange::CreateCbv(int index, const D3D12_CONSTANT_BUFFER_VIEW_DESC* cbv_desc)
{
    assert(IsWithinBounds(index));
    device->CreateConstantBufferView(cbv_desc, GetCpuHandle(index));
}

void DescriptorRange::CreateSrv(int index, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc)
{
    assert(IsWithinBounds(index));
    device->CreateShaderResourceView(resource, srv_desc, GetCpuHandle(index));
}

void DescriptorRange::CreateUav(int index, ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc)
{
    assert(IsWithinBounds(index));
    device->CreateUnorderedAccessView(resource, nullptr, uav_desc, GetCpuHandle(index));
}

void DescriptorRange::CreateSampler(int index, const D3D12_SAMPLER_DESC* sampler_desc)
{
    assert(IsWithinBounds(index));
    device->CreateSampler(sampler_desc, GetCpuHandle(index));
}

void DescriptorRange::CreateRtv(int index, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* rtv_desc)
{
    assert(IsWithinBounds(index));
    device->CreateRenderTargetView(resource, rtv_desc, GetCpuHandle(index));
}

void DescriptorRange::CreateDsv(int index, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* dsv_desc)
{
    assert(IsWithinBounds(index));
    device->CreateDepthStencilView(resource, dsv_desc, GetCpuHandle(index));
}

bool DescriptorRange::IsWithinBounds(int index) const
{
    return (index >= this->descriptor_start) && (index < (this->descriptor_start + this->capacity));
}

void DescriptorStack::Create(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc)
{
    DescriptorRange::Create(device, desc);
    this->size = 0;
}

void DescriptorStack::Create(ID3D12DescriptorHeap* descriptor_heap, int start, int capacity)
{
    DescriptorRange::Create(descriptor_heap, start, capacity);
    this->size = 0;
}

void DescriptorStack::Create(DescriptorRange* descriptor_range, int start, int capacity)
{
    DescriptorRange::Create(descriptor_range, start, capacity);
    this->size = 0;
}

int DescriptorStack::Allocate(int num_of_descriptors)
{
    int new_size = this->size + num_of_descriptors;
    if (new_size > capacity) {
        return -1;
    }
    int index = this->descriptor_start + this->size;
    this->size = new_size;
    return index;
}

void DescriptorStack::Reset()
{
    this->size = 0;
}

int DescriptorStack::Size() const
{
    return this->size;
}

void DescriptorPool::Create(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc)
{
    DescriptorRange::Create(device, desc);
    Reset();
    #ifdef DEBUG
    used_descriptors = std::vector<bool>(this->capacity, false);
    #endif
}

void DescriptorPool::Create(ID3D12DescriptorHeap* descriptor_heap, int start, int capacity)
{
    DescriptorRange::Create(descriptor_heap, start, capacity);
    Reset();
    #ifdef DEBUG
    used_descriptors = std::vector<bool>(this->capacity, false);
    #endif
}

void DescriptorPool::Create(DescriptorRange* descriptor_range, int start, int capacity)
{
    DescriptorRange::Create(descriptor_range, start, capacity);
    Reset();
    #ifdef DEBUG
    used_descriptors = std::vector<bool>(this->capacity, false);
    #endif
}

int DescriptorPool::Allocate()
{
    if (this->free_descriptors.empty()) {
        return -1;
    }
    this->size++;
    int descriptor = free_descriptors.back();
    this->free_descriptors.pop_back();
    #ifdef DEBUG
    assert(this->used_descriptors[descriptor] == false);
    this->used_descriptors[descriptor] = true;
    #endif
    return descriptor;
}

int DescriptorPool::AllocateAndCreateCbv(const D3D12_CONSTANT_BUFFER_VIEW_DESC* cbv_desc)
{
    int descriptor = Allocate();
    if (descriptor != -1) {
        CreateCbv(descriptor, cbv_desc);
    }
    return descriptor;
}

int DescriptorPool::AllocateAndCreateSrv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc)
{
    int descriptor = Allocate();
    if (descriptor != -1) {
        CreateSrv(descriptor, resource, srv_desc);
    }
    return descriptor;
}

int DescriptorPool::AllocateAndCreateUav(ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc)
{
    int descriptor = Allocate();
    if (descriptor != -1) {
        CreateUav(descriptor, resource, counter_resource, uav_desc);
    }
    return descriptor;
}

int DescriptorPool::AllocateAndCreateRtv(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* rtv_desc)
{
    int descriptor = Allocate();
    if (descriptor != -1) {
        CreateRtv(descriptor, resource, rtv_desc);
    }
    return descriptor;
}

int DescriptorPool::AllocateAndCreateDsv(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* dsv_desc)
{
    int descriptor = Allocate();
    if (descriptor != -1) {
        CreateDsv(descriptor, resource, dsv_desc);
    }
    return descriptor;
}

void DescriptorPool::Free(int index)
{
    if (index != -1) {
        assert((index >= this->descriptor_start) && (index < (this->descriptor_start + this->capacity)));
        assert(this->size != 0);
        this->size--;
        this->free_descriptors.push_back(index);
        #ifdef DEBUG
        assert(this->used_descriptors[index] == true); // Double free.
        this->used_descriptors[index] = false;
        #endif
    }
}

void DescriptorPool::Reset()
{
    this->size = 0;
    this->free_descriptors.clear();
    for (int i = this->descriptor_start + this->capacity; (i--) > this->descriptor_start;) {
        this->free_descriptors.push_back(i);
    }
}

int DescriptorPool::Size() const
{
    return this->size;
}
