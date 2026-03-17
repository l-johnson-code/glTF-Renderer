#pragma once

#include <memory>
#include <vector>

#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>
#include <wrl.h>

#include "Memory.h"
#include "TlsfHeap.h"

class GpuAllocation;
struct GpuResource;

class GpuAllocator {
    public:

    HRESULT Init(ID3D12Device* device);
    void DeInit();
    bool SupportsGpuUploadHeap() const
    {
        return supports_gpu_upload_heap;
    }
    HRESULT CreateResource(const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value, GpuResource* resource, const char* name = nullptr);
    HRESULT CreateCommittedResource(const D3D12_HEAP_PROPERTIES* heap_properties, D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value, GpuResource* resource, const char* name = nullptr);
    HRESULT Allocate(uint64_t size, uint64_t alignment, int* heap_index, TlsfHeap::Allocation* allocation);
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

    GpuAllocation(GpuAllocator* allocator, int heap, void* handle)
    {
        this->is_committed = false;
        this->allocator = allocator;
        this->heap = heap;
        this->handle = handle;
    }

    ~GpuAllocation()
    {
        Free();
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
    void* handle = nullptr;
};

struct GpuResource {

	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	std::shared_ptr<GpuAllocation> allocation;

    void Reset()
    {
        resource.Reset();
        allocation.reset();
    }
};

