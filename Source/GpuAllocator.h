#pragma once

#include <memory>
#include <vector>

#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>
#include <wrl.h>

#include "Memory.h"
#include "TlsfHeap.h"

class GpuAllocation;

class GpuAllocator {
    public:

    HRESULT Init(ID3D12Device* device);
    void DeInit();
    bool SupportsGpuUploadHeap() const
    {
        return supports_gpu_upload_heap;
    }
    HRESULT CreateResource(D3D12_HEAP_TYPE heap_type, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value, const char* name, GpuAllocation* allocation, REFIID iid, void** resource);
    void Free(GpuAllocation* allocation);
    
    private:
    
    static constexpr uint64_t heap_size = Mebibytes(256);
    
    bool supports_gpu_upload_heap = false;
    
    size_t local_capacity = 0;
    size_t non_local_capacity = 0;
    
    size_t local_budget = 0;
    size_t non_local_budget = 0;
    
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    std::vector<TlsfHeap> heaps;

    HRESULT Allocate(uint64_t size, uint64_t alignment, int* heap_index, TlsfHeap::Allocation* allocation);
};

class GpuAllocation {
    public:

    GpuAllocation() = default;
    GpuAllocation(GpuAllocator* allocator, ID3D12Resource* resource)
    {
        this->is_committed = true;
        this->allocator = allocator;
        this->resource = resource;
        this->resource->AddRef();
    }

    GpuAllocation(GpuAllocator* allocator, ID3D12Resource* resource, int heap, TlsfHeap::NodeIndex handle)
    {
        this->is_committed = false;
        this->allocator = allocator;
        this->resource = resource;
        this->resource->AddRef();
        this->heap = heap;
        this->handle = handle;
    }

    void Free() 
    {
        if (allocator) {
            allocator->Free(this);
        }
    }

    GpuAllocator* allocator = nullptr;
    ID3D12Resource* resource = nullptr;
    bool is_committed = false;
    int heap = 0;
    TlsfHeap::NodeIndex handle = TlsfHeap::null_block_index;
};

