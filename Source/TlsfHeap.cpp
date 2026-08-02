#include "TlsfHeap.h"

#include <bit>

#include <directx/d3dx12_core.h>

#include "Memory.h"
#include "Profiling.h"

// TODO: Handle zeros.
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

void TlsfHeap::Init(ID3D12Device* device, uint32_t heap_size)
{
    this->size = 0;

    // Create the underlying heap.
    CD3DX12_HEAP_DESC heap_desc(heap_size, D3D12_HEAP_TYPE_DEFAULT);
    HRESULT result = CreateHeap(device, &heap_desc, &heap);
    this->capacity = heap_size;

    // Clear the free lists and bitmaps.
    first_level_bitmap = 0;
    for (int i = 0; i < second_level_bins; i++) {
        second_level_bitmaps[i] = 0;
    }
    for (int i = 0; i < first_level_bins; i++) {
        for (int j = 0; j < second_level_bins; j++) {
            free_lists[i][j] = null_block_index;
        }
    }

    // Create an initial free block.
    NodeIndex initial_block_index = blocks.Emplace();
    blocks[initial_block_index] = {
        .offset = 0,
        .size = capacity,
        .next = null_block_index,
        .previous = null_block_index,
        .is_occupied = false,
    };
    InsertFreeBlock(initial_block_index);
    first_block = initial_block_index;
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

TlsfHeap::Allocation TlsfHeap::Allocate(uint32_t size, uint32_t alignment)
{
    Allocation allocation = {
        .handle = null_block_index,
        .offset = 0,
    };

    // Allocate extra space so that we can properly align the allocation.
    uint32_t required_size = size + alignment - 1;
    
    // Try to get a block from a free list.
    NodeIndex block_index = GetGoodFitBlock(required_size);
    if (block_index != null_block_index) {
        // We take a copy of the block rather than a reference because we may allocate a new block later in the function, possibly invalidating any block references.
        Block block = blocks[block_index];

        // Remove this block from the free list.
        RemoveFreeBlock(block_index);

        allocation.handle = block_index;
        allocation.offset = AlignPowerOfTwo(block.offset, alignment);

        // Create a or expand a free block to the left.
        if (allocation.offset > block.offset) {
            if (block.previous != null_block_index && !blocks[block.previous].is_occupied) {
                // Expand previous free block.
                blocks[block.previous].size = allocation.offset - blocks[block.previous].offset;
            } else {
                // Create a free block.
                NodeIndex free_block_index = blocks.Emplace();
                Block& free_block = blocks[free_block_index];
                free_block.size = allocation.offset - block.offset;
                free_block.offset = block.offset;
                free_block.is_occupied = false;
                InsertBefore(block_index, free_block_index);
                InsertFreeBlock(free_block_index);
            }
            // Trim space off the beginning of the block.
            block.size -= (allocation.offset - block.offset);
        }

        // Create free block to the right.
        if (block.size > required_size) {
            // Split the block into two.
            NodeIndex free_block_index = blocks.Emplace();
            Block& free_block = blocks[free_block_index];
            free_block.offset = allocation.offset + size;
            free_block.size = block.size - size;
            free_block.is_occupied = false;
            InsertAfter(block_index, free_block_index);
            InsertFreeBlock(free_block_index);
        }

        blocks[block_index].offset = allocation.offset;
        blocks[block_index].size = size;
        blocks[block_index].is_occupied = true;

        this->size += size;
    }

    return allocation;
}

void TlsfHeap::Free(NodeIndex handle)
{
    if (handle != null_block_index) {
        Block& block = blocks[handle];

        this->size -= block.size;

        assert(block.is_occupied);
        block.is_occupied = false;

        // Merge left.
        NodeIndex previous_index = block.previous;
        if (previous_index != null_block_index && !blocks[previous_index].is_occupied) {
            block.offset = blocks[previous_index].offset;
            block.size += blocks[previous_index].size;
            RemoveBlock(previous_index);
            RemoveFreeBlock(previous_index);
            blocks.Erase(previous_index);
        }

        // Merge right.
        NodeIndex next_index = block.next;
        if (next_index != null_block_index && !blocks[next_index].is_occupied) {
            block.size += blocks[next_index].size;
            RemoveBlock(next_index);
            RemoveFreeBlock(next_index);
            blocks.Erase(next_index);
        }

        InsertFreeBlock(handle);
    }
}

uint8_t TlsfHeap::FirstLevelIndex(uint32_t size)
{
    return MostSignificantBitIndex(size >> significand_bits);
}

uint8_t TlsfHeap::SecondLevelIndex(uint32_t size, uint32_t first_level_index)
{
    return (size >> first_level_index) - (1 << significand_bits);
}

TlsfHeap::NodeIndex TlsfHeap::GetGoodFitBlock(uint32_t size)
{
    // Get the indexes corresponding to the smallest bin that can contain our allocation.
    size = std::max(size, 1u << significand_bits);
    size += std::numeric_limits<uint32_t>::max() >> significand_bits >> 1 >> std::countl_zero(size);
    uint8_t first_level_index = FirstLevelIndex(size);
    uint8_t second_level_index = SecondLevelIndex(size, first_level_index);

    // Look for a free block from the bins with the same exponent.
    uint32_t first_level_bitmask = 1 << first_level_index;
    uint16_t second_level_bitmask = std::numeric_limits<uint16_t>::max() << second_level_index;
    if ((first_level_bitmap & first_level_bitmask) && (second_level_bitmaps[first_level_index] & second_level_bitmask)) {
        second_level_index = LeastSignificantBitIndex(second_level_bitmaps[first_level_index] & second_level_bitmask);
        return free_lists[first_level_index][second_level_index];
    }

    // Look for a free block from the larger bins.
    first_level_bitmask = std::numeric_limits<uint32_t>::max() << first_level_index << 1;
    first_level_index = LeastSignificantBitIndex(first_level_bitmap & first_level_bitmask);
    if (first_level_index != 64) {
        second_level_index = LeastSignificantBitIndex(second_level_bitmaps[first_level_index]);
        if (second_level_index != 64) {
            return free_lists[first_level_index][second_level_index];
        }
    }

    return null_block_index;
}

void TlsfHeap::InsertAfter(NodeIndex before_index, NodeIndex after_index)
{
    Block& before = blocks[before_index];
    Block& after = blocks[after_index];
    after.next = before.next;
    if (after.next != null_block_index) {
        blocks[after.next].previous = after_index;
    }
    after.previous = before_index;
    before.next = after_index;
}

void TlsfHeap::InsertBefore(NodeIndex after_index, NodeIndex before_index)
{
    Block& before = blocks[before_index];
    Block& after = blocks[after_index];
    before.previous = after.previous;
    if (before.previous != null_block_index) {
        blocks[before.previous].next = before_index; 
    } else {
        first_block = before_index;
    }
    after.previous = before_index;
    before.next = after_index;
}

void TlsfHeap::RemoveBlock(NodeIndex block_index)
{
    const Block& block = blocks[block_index];
    if (block.previous != null_block_index) {
        blocks[block.previous].next = block.next;
    } else {
        first_block = block.next;
    }
    if (block.next != null_block_index) {
        blocks[block.next].previous = block.previous;
    }
}

void TlsfHeap::InsertFreeBlock(NodeIndex block_index)
{
    Block& block = blocks[block_index];
    assert(!block.is_occupied);

    // Get the largest bin size that is smaller than the block size.
    if (block.size >= (1 << significand_bits)) {
        uint8_t first_level_index = FirstLevelIndex(block.size);
        uint8_t second_level_index = SecondLevelIndex(block.size, first_level_index);

        // Add block to top of the free list.
        block.next_free = free_lists[first_level_index][second_level_index];
        if (block.next_free != null_block_index) {
            blocks[block.next_free].previous_free = block_index;
        }
        free_lists[first_level_index][second_level_index] = block_index;
        block.previous_free = null_block_index;

        // Update the bitmaps.
        first_level_bitmap |= 1 << first_level_index;
        second_level_bitmaps[first_level_index] |= 1 << second_level_index;
    } else {
        // Denormals are not added to the free list.
        block.next_free = null_block_index;
        block.previous_free = null_block_index;
    }
}

void TlsfHeap::RemoveFreeBlock(NodeIndex block_index)
{
    Block& block = blocks[block_index];
    assert(!block.is_occupied);
    if (block.next_free != null_block_index) {
        blocks[block.next_free].previous_free = block.previous_free;
    }
    if (block.previous_free != null_block_index) {
        blocks[block.previous_free].next_free = block.next_free;
    } else if (block.size >= (1 << significand_bits)) {
        // The block is at the top of the free list.
        uint8_t first_level_index = FirstLevelIndex(block.size);
        uint8_t second_level_index = SecondLevelIndex(block.size, first_level_index);

        free_lists[first_level_index][second_level_index] = block.next_free;
        if (block.next_free == null_block_index) {
            // Update the bitmaps.
            second_level_bitmaps[first_level_index] &= ~(1 << second_level_index);
            if (second_level_bitmaps[first_level_index] == 0) {
                first_level_bitmap &= ~(1 << first_level_index);
            }
        }
    }
    block.next_free = null_block_index; // TODO: Is this really necessary?
    block.previous_free = null_block_index;
}