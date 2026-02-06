#pragma once

#include <vector>
#include <span>

#include <wrl.h>
#include <directx/d3d12.h>

#include "BufferAllocator.h"
#include "DescriptorAllocator.h"

class DescriptorSpan 
{
    public:
    DescriptorSpan()
    {
        this->start = 0;
        this->count = 0;
    }

    DescriptorSpan(int start, int count)
    {
        this->start = start;
        this->count = count;
    }

    int operator[](int index)
    {
        assert((index >= 0) && (index < count));
        return start + index;
    }

    bool IsEmpty()
    {
        return count == 0;
    }

    private:
    int start = 0;
    int count = 0;
};

class CommandContext
{
    public:

    void Init(ID3D12GraphicsCommandList4* command_list, CbvSrvUavStack* transient_descriptors, CpuMappedLinearBuffer* transient_allocator, std::vector<D3D12_RESOURCE_BARRIER>* barriers);

    // Debug markers.
    #if USE_PIX
    void InsertMarker(const char* name);
    void BeginEvent(const char* name);
    void EndEvent();
    #else
    void InsertMarker(const char* name) {}
    void BeginEvent(const char* name) {}
    void EndEvent() {}
    #endif

    // Barriers.
    void PushTransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before_state, D3D12_RESOURCE_STATES after_state, uint32_t subresource = std::numeric_limits<uint32_t>::max(), D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE);
    void PushUavBarrier(ID3D12Resource* resource);
    void SubmitBarriers();

    // Allocations.
    void* Allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS* gpu_ptr);
    D3D12_GPU_VIRTUAL_ADDRESS AllocateAndCopy(const void* data, uint64_t size, uint64_t alignment);
    D3D12_GPU_VIRTUAL_ADDRESS CreateConstantBuffer(const void* constant_buffer, size_t size);
    template<typename T>
    D3D12_GPU_VIRTUAL_ADDRESS CreateConstantBuffer(const T* constant_buffer)
    {
        return CreateConstantBuffer(constant_buffer, sizeof(T));
    }
    DescriptorSpan AllocateDescriptors(int count);
    void CreateSrv(int index, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc);
    void CreateUav(int index, ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc);
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> command_list;
    
    private:
    CbvSrvUavStack* transient_descriptors = nullptr;
    CpuMappedLinearBuffer* transient_allocator = nullptr;
    std::vector<D3D12_RESOURCE_BARRIER>* barriers = nullptr;
};