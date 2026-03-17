#pragma once

#include <directx/d3d12.h>

#include "Pool.h"

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