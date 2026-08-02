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
    
    const std::vector<TlsfHeap>& Heaps() const
    {
        return heaps;
    }

    size_t LocalCapacity() const
    {
        return local_capacity;
    }

    size_t NonLocalCapacity() const
    {
        return non_local_capacity;
    }

    size_t LocalBudget() const
    {
        return local_budget;
    }

    size_t NonLocalBudget() const
    {
        return non_local_budget;
    }

    // Total amount of memory allocated in GPU local memory. This includes unused heap space.
    size_t LocalAllocated() const
    {
        size_t total = committed_local_size;
        for (const TlsfHeap& heap: heaps) {
            total += heap.Capacity();
        }
        return total;
    }

    // Total amount of memory allocated in non local memory. This includes unused heap space.
    size_t NonLocalAllocated() const
    {
        return committed_non_local_size;
    }

    // Total amount of memory that is actually used by resources in GPU local memory.
    size_t LocalUsed() const
    {
        size_t total = committed_local_size;
        for (const TlsfHeap& heap: heaps) {
            total += heap.Size();
        }
        return total;
    }

    // Total amount of memory that is actually used by resources in non local memory.
    size_t NonLocalUsed() const
    {
        return committed_non_local_size;
    }
    
    private:
    
    static constexpr uint64_t heap_size = Mebibytes(256);
    
    bool supports_gpu_upload_heap = false;
    
    size_t local_capacity = 0;
    size_t non_local_capacity = 0;
    
    size_t local_budget = 0;
    size_t non_local_budget = 0;

    size_t committed_local_size = 0;
    size_t committed_non_local_size = 0;

    Microsoft::WRL::ComPtr<ID3D12Device> device;
    std::vector<TlsfHeap> heaps;

    HRESULT Allocate(uint64_t size, uint64_t alignment, int* heap_index, TlsfHeap::Allocation* allocation);
};

class GpuAllocation {
    public:

    GpuAllocation() = default;
    GpuAllocation(GpuAllocator* allocator, ID3D12Resource* resource, uint64_t size, bool local)
    {
        this->is_committed = true;
        this->allocator = allocator;
        this->resource = resource;
        this->resource->AddRef();
        this->committed.size = size;
        this->committed.local = local;
    }

    GpuAllocation(GpuAllocator* allocator, ID3D12Resource* resource, int heap, TlsfHeap::NodeIndex handle)
    {
        this->is_committed = false;
        this->allocator = allocator;
        this->resource = resource;
        this->resource->AddRef();
        this->placed.heap = heap;
        this->placed.handle = handle;
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
    union {
        struct {
            int heap;
            TlsfHeap::NodeIndex handle;
        } placed;
        struct {
            uint64_t size;
            bool local;
        } committed;
    };
};

