#pragma once

#include <memory>
#include <vector>

#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>
#include <wrl.h>

#include "Memory.h"
#include "Pool.h"

class GpuAllocation;
struct GpuResource;

class TlsfHeap {

    public:

    struct Allocation {
        void* handle;
        uint64_t offset;
    };
    
    ID3D12Heap* heap = nullptr;

    void Init(ID3D12Device* device, uint64_t heap_size, uint32_t max_allocations);
    void DeInit();
    Allocation Allocate(uint64_t size, uint64_t alignment);
    void Free(void* handle);

    private:

    struct Block {
        uint64_t offset;
        uint64_t size;
        Block* next;
        Block* previous;
        Block* next_free;
        Block* previous_free;
        bool is_occupied;
    };

    static constexpr uint8_t significand_bits = 4;
    static constexpr uint8_t exponent_bits = 5;
    static constexpr uint8_t second_level_bins = 1 << significand_bits;
    static constexpr uint8_t first_level_bins = 1 << exponent_bits;
    static constexpr uint8_t max_significand_value = (2 << significand_bits) - 1;
    static constexpr uint8_t max_exponent_value = (1 << exponent_bits) - 1;
    static constexpr uint64_t max_allocation_size = max_significand_value << max_exponent_value;

    uint64_t capacity = 0;
    uint64_t size = 0;

    uint32_t first_level_bitmap = 0;
    uint16_t second_level_bitmaps[first_level_bins] = {};

    Block* free_lists[first_level_bins][second_level_bins] = {};

    Pool<Block> blocks;

    uint8_t FirstLevelIndex(uint64_t size);
    uint8_t SecondLevelIndex(uint64_t size, uint32_t first_level_index);
    Block* GetGoodFitBlock(uint64_t size);
    void InsertAfter(Block* block, Block* new_block);
    void InsertBefore(Block* block, Block* new_block);
    void RemoveBlock(Block* block);
    void InsertFreeBlock(Block* block);
    void RemoveFreeBlock(Block* block);
};

class GpuAllocator {
    public:

    void Init(ID3D12Device* device);
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

