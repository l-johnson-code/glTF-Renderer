#include "GpuAllocator.h"

#include <bit>

#include "Profiling.h"
#include "DirectXHelpers.h"

// TODO: Handle zeros
static uint8_t MostSignificantBitIndex(uint64_t value)
{
    return 63 - std::countl_zero(value);
}

static uint8_t LeastSignificantBitIndex(uint64_t value)
{
    return std::countr_zero(value);
}

static Profiling::MemoryPool GetPoolFromHeapProperties(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heap_properties)
{
    D3D12_HEAP_PROPERTIES detailed_heap_properties;
    if (heap_properties->Type == D3D12_HEAP_TYPE_CUSTOM) {
        detailed_heap_properties = *heap_properties;
    } else {
        detailed_heap_properties = device->GetCustomHeapProperties(0, heap_properties->Type);
    }
    return detailed_heap_properties.MemoryPoolPreference == D3D12_MEMORY_POOL_L0 ? Profiling::MEMORY_POOL_CPU : Profiling::MEMORY_POOL_GPU;
}

static HRESULT CreateHeap(ID3D12Device* device, const D3D12_HEAP_DESC* desc, ID3D12Heap** heap)
{
	ProfileZoneScoped();
	HRESULT result = device->CreateHeap(desc, IID_PPV_ARGS(heap));
	if (SUCCEEDED(result)) {
		Profiling::MemoryPool pool = GetPoolFromHeapProperties(device, &desc->Properties);
		ProfileAllocP((*heap), desc->SizeInBytes, pool);
	}
	return result;
}

static void DestroyHeap(ID3D12Heap* heap)
{
    ProfileZoneScoped();
    if (heap) {
        D3D12_HEAP_DESC desc = heap->GetDesc();
        assert(desc.Properties.Type == D3D12_HEAP_TYPE_CUSTOM);
        Profiling::MemoryPool pool = desc.Properties.MemoryPoolPreference == D3D12_MEMORY_POOL_L0 ? Profiling::MEMORY_POOL_CPU : Profiling::MEMORY_POOL_GPU;
        ProfileFreeP(heap, pool);
        heap->Release();
    }
}

void TlsfHeap::Init(ID3D12Device* device, uint64_t heap_size, uint32_t max_allocations)
{
    this->size = 0;

    // Create the underlying heap.
    CD3DX12_HEAP_DESC heap_desc(heap_size, D3D12_HEAP_TYPE_DEFAULT);
    HRESULT result = CreateHeap(device, &heap_desc, &heap);
    this->capacity = heap_size;

    // Allocate pool for blocks.
    uint32_t max_blocks = (2 * max_allocations) + 1;
    this->blocks.Init(max_allocations);

    // Clear the free lists and bitmaps.
    first_level_bitmap = 0;
    for (int i = 0; i < second_level_bins; i++) {
        second_level_bitmaps[i] = 0;
    }
    for (int i = 0; i < first_level_bins; i++) {
        for (int j = 0; j < second_level_bins; j++) {
            free_lists[i][j] = nullptr;
        }
    }

    // Create an initial free block.
    Block* initial_block = blocks.Construct();
    initial_block->size = capacity;
    initial_block->offset = 0;
    initial_block->previous = nullptr;
    initial_block->next = nullptr;
    initial_block->is_occupied = false;
    InsertFreeBlock(initial_block);
}

void TlsfHeap::DeInit()
{
    if (heap) {
        DestroyHeap(heap);
        heap = nullptr;
    }
    this->size = 0;
    this->capacity = 0;
    this->first_level_bitmap = 0;
}

TlsfHeap::Allocation TlsfHeap::Allocate(uint64_t size, uint64_t alignment)
{
    Allocation allocation = {
        .handle = nullptr,
        .offset = 0,
    };

    // Allocate extra space so that we can properly align the allocation.
    uint64_t required_size = size + alignment - 1;
    
    // Try to get a block from a free list.
    Block* block = GetGoodFitBlock(required_size);
    if (block) {

        // Remove this block from the free list and mark as occupied.
        RemoveFreeBlock(block);
        block->is_occupied = true;

        allocation.handle = block;
        allocation.offset = AlignPowerOfTwo(block->offset, alignment);

        // Create a or expand a free block to the left.
        if (allocation.offset > block->offset) {
            if (block->previous && !block->previous->is_occupied) {
                // Expand previous free block.
                block->previous->size = allocation.offset - block->previous->offset;
            } else {
                // Create a free block.
                Block* free_block = blocks.Construct();
                free_block->size = allocation.offset - block->offset;
                free_block->offset = block->offset;
                free_block->is_occupied = false;
                InsertBefore(block, free_block);
                InsertFreeBlock(free_block);
            }
            // Trim space off the beginning of the block.
            block->size -= (allocation.offset - block->offset);
            block->offset = allocation.offset;
        }

        // Create free block to the right.
        if (block->size > required_size) {
            // Split the block into two.
            Block* free_block = blocks.Construct();
            free_block->offset = block->offset + size;
            free_block->size = block->size - size;
            free_block->is_occupied = false;
            InsertAfter(block, free_block);
            InsertFreeBlock(free_block);
            // Trim space off the end of the block.
            block->size = size;
        }
    }

    return allocation;
}

void TlsfHeap::Free(void* handle)
{
    if (handle) {
        Block* block = reinterpret_cast<Block*>(handle);

        assert(block->is_occupied);
        block->is_occupied = false;

        // Merge left.
        Block* previous = block->previous;
        if (previous && !previous->is_occupied) {
            block->offset = previous->offset;
            block->size += previous->size;
            RemoveBlock(previous);
            RemoveFreeBlock(previous);
            blocks.Destroy(previous);
        }

        // Merge right.
        Block* next = block->next;
        if (next && !next->is_occupied) {
            block->size += next->size;
            RemoveBlock(next);
            RemoveFreeBlock(next);
            blocks.Destroy(next);
        }

        InsertFreeBlock(block);
    }
}

uint8_t TlsfHeap::FirstLevelIndex(uint64_t size)
{
    return MostSignificantBitIndex(size >> significand_bits);
}

uint8_t TlsfHeap::SecondLevelIndex(uint64_t size, uint32_t first_level_index)
{
    return (size >> first_level_index) - (1 << significand_bits);
}

TlsfHeap::Block* TlsfHeap::GetGoodFitBlock(uint64_t size)
{
    // Get the indexes corresponding to the smallest bin that can contain our allocation.
    size = size < (1 << significand_bits) ? (1 << significand_bits) : size; // Round up size to smallest possible bin size.
    size += (1 << MostSignificantBitIndex(size >> significand_bits)) - 1; // Round up to next bin size.

    uint8_t first_level_index = FirstLevelIndex(size);
    uint8_t second_level_index = SecondLevelIndex(size, first_level_index);

    if ((first_level_bitmap & (1 << first_level_index)) && (second_level_bitmaps[first_level_index] & (-1 << second_level_index))) {
        second_level_index = LeastSignificantBitIndex(second_level_bitmaps[first_level_index]);
        return free_lists[first_level_index][second_level_index];
    }

    first_level_index = LeastSignificantBitIndex(first_level_bitmap & (-1 << (first_level_index + 1)));
    if (first_level_index != -1) {
        second_level_index = LeastSignificantBitIndex(second_level_bitmaps[first_level_index]);
        if (second_level_index != -1) {
            return free_lists[first_level_index][second_level_index];
        }
    }

    return nullptr;
}

void TlsfHeap::InsertAfter(Block* block, Block* new_block)
{
    new_block->next = block->next;
    new_block->previous = block;
    block->next = new_block;
}

void TlsfHeap::InsertBefore(Block* block, Block* new_block)
{
    new_block->previous = block->previous;
    new_block->next = block;
    block->previous = new_block;
}

void TlsfHeap::RemoveBlock(Block* block)
{
    if (block->previous) {
        block->previous->next = block->next;
    }
    if (block->next) {
        block->next->previous = block->previous;
    }
}

void TlsfHeap::InsertFreeBlock(Block* block)
{
    assert(!block->is_occupied);

    // Get the largest bin size that is smaller than the block size.
    if (block->size >= (1 << significand_bits)) {
        uint8_t first_level_index = FirstLevelIndex(block->size);
        uint8_t second_level_index = SecondLevelIndex(block->size, first_level_index);

        // Add block to top of the free list.
        block->next_free = free_lists[first_level_index][second_level_index];
        free_lists[first_level_index][second_level_index] = block;
        block->previous_free = nullptr;

        // Update the bitmaps.
        first_level_bitmap |= 1 << first_level_index;
        second_level_bitmaps[first_level_index] |= 1 << second_level_index;
    } else {
        // Denormals are not added to the free list.
        block->next_free = nullptr;
        block->previous_free = nullptr;
    }
}

void TlsfHeap::RemoveFreeBlock(Block* block)
{
    assert(!block->is_occupied);
    if (block->next_free) {
        block->next_free->previous_free = block->previous_free;
    }
    if (block->previous_free) {
        block->previous_free->next_free = block->next_free;
    } else {
        // The block is at the top of the free list.
        uint8_t first_level_index = FirstLevelIndex(block->size);
        uint8_t second_level_index = SecondLevelIndex(block->size, first_level_index);

        free_lists[first_level_index][second_level_index] = block->next_free;
        if (!block->next_free) {
            // Update the bitmaps.
            second_level_bitmaps[first_level_index] &= ~(1 << second_level_index);
            if (second_level_bitmaps[first_level_index] == 0) {
                first_level_bitmap &= ~(1 << first_level_index);
            }
        }
    }
}

void GpuAllocator::Init(ID3D12Device* device)
{
    this->device = device;
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options = {};
    HRESULT result = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options, sizeof(options));
    this->supports_gpu_upload_heap = SUCCEEDED(result) && options.GPUUploadHeapSupported;
}

HRESULT GpuAllocator::CreateResource(const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value, GpuResource* resource, const char* name)
{
    ProfileZoneScoped();

    // TODO: Support tight alignment.
    D3D12_RESOURCE_ALLOCATION_INFO allocation_info = device->GetResourceAllocationInfo(0, 1, desc);

    int heap_index = 0;
    TlsfHeap::Allocation heap_allocation;
    HRESULT result = Allocate(allocation_info.SizeInBytes, allocation_info.Alignment, &heap_index, &heap_allocation);
    if (FAILED(result)) {
        return result;
    }

    result = this->device->CreatePlacedResource(heaps[heap_index].heap, heap_allocation.offset, desc, initial_state, optimized_clear_value, IID_PPV_ARGS(&resource->resource));
    if (FAILED(result)) {
        heaps[heap_index].Free(heap_allocation.handle);
        return result;
    }

    if (name) {
        SetName(resource->resource.Get(), name);
    }

    resource->allocation = std::make_shared<GpuAllocation>(this, heap_index, heap_allocation.handle);

    return S_OK;
}

HRESULT GpuAllocator::CreateCommittedResource(const D3D12_HEAP_PROPERTIES* heap_properties, D3D12_HEAP_FLAGS heap_flags, const D3D12_RESOURCE_DESC* desc, D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE *optimized_clear_value, GpuResource* resource, const char* name)
{
    ProfileZoneScoped();

	HRESULT result = device->CreateCommittedResource(heap_properties, heap_flags, desc, initial_state, optimized_clear_value, IID_PPV_ARGS(&resource->resource));
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return result;
	}

    if (name) {
        SetName(resource->resource.Get(), name);
    }

	// Estimate the size of the resource. 
	// This value may be lower or higher than the underlying heap size but there appears to be no way to directly query the size of the underlying heap.
	D3D12_RESOURCE_ALLOCATION_INFO alloc_info = device->GetResourceAllocationInfo(0, 1, desc);

    // Log the allocation in tracy.
	Profiling::MemoryPool pool = GetPoolFromHeapProperties(this->device.Get(), heap_properties);
	ProfileAllocP(resource->resource.Get(), alloc_info.SizeInBytes, pool);

    resource->allocation = std::make_shared<GpuAllocation>(this, resource->resource.Get());

    return S_OK;
}

HRESULT GpuAllocator::Allocate(uint64_t size, uint64_t alignment, int* heap_index, TlsfHeap::Allocation* allocation)
{
    // Try to fit in an existing heap.
    for (int i = 0; i < heaps.size(); i++) {
        TlsfHeap& heap = heaps[i];
        *allocation = heap.Allocate(size, alignment);
        if (allocation->handle) {
            *heap_index = i;
            return S_OK;
        }
    }

    // Create a new heap if we cant.
    TlsfHeap& heap = heaps.emplace_back();
    heap.Init(this->device.Get(), this->heap_size, 65536);
    *allocation = heap.Allocate(size, alignment);
    if (allocation->handle) {
        *heap_index = heaps.size() - 1;
        return S_OK;
    }

    return E_FAIL;
}

void GpuAllocator::Free(GpuAllocation* allocation)
{
    ProfileZoneScoped();
    if (allocation->allocator) {
        if (allocation->is_committed && allocation->resource) {
            D3D12_HEAP_PROPERTIES heap_properties;
            HRESULT result = allocation->resource->GetHeapProperties(&heap_properties, nullptr);
            assert(SUCCEEDED(result));
            Profiling::MemoryPool pool = GetPoolFromHeapProperties(this->device.Get(), &heap_properties);
            ProfileFreeP(allocation->resource, pool);
            allocation->resource->Release();
            allocation->resource = nullptr;
        } else if (allocation->handle){
            TlsfHeap& heap = heaps[allocation->heap];
            heap.Free(allocation->handle);
        }
    }
}