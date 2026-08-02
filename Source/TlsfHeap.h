#pragma once

#include <limits>

#include <directx/d3d12.h>

#include "Pool.h"

class TlsfHeap {

    public:

    using NodeIndex = uint32_t;

    struct Allocation {
        NodeIndex handle;
        uint32_t offset;
    };
    
    struct Block {
        uint32_t offset;
        uint32_t size;
        NodeIndex next;
        NodeIndex previous;
        NodeIndex next_free;
        NodeIndex previous_free;
        bool is_occupied;
    };

    class ConstIterator {
        public:

        ConstIterator& operator++()
        {
            assert(block != null_block_index);
            block = heap->blocks[block].next;
            return *this;
        }

        const Block& operator*()
        {
            assert(block != null_block_index);
            return heap->blocks[block];
        }

        bool operator==(const ConstIterator& iterator)
        {
            return (this->heap == iterator.heap) && (this->block == iterator.block);
        }

        bool operator!=(const ConstIterator& iterator)
        {
            return (this->heap != iterator.heap) || (this->block != iterator.block);
        }
        
        private:

        friend class TlsfHeap;

        const TlsfHeap* heap = nullptr;
        uint32_t block = null_block_index;

        ConstIterator(const TlsfHeap* heap, uint32_t block) 
        {
            this->heap = heap;
            this->block = block;
        }
    };

    static constexpr NodeIndex null_block_index = std::numeric_limits<NodeIndex>::max();

    ID3D12Heap* heap = nullptr;

    void Init(ID3D12Device* device, uint32_t heap_size);
    void DeInit();
    Allocation Allocate(uint32_t size, uint32_t alignment);
    void Free(NodeIndex handle);

    uint32_t Size() const 
    {
        return size;
    }

    uint32_t Capacity() const
    {
        return capacity;
    }

    ConstIterator begin() const
    {
        return ConstIterator(this, first_block);
    }

    ConstIterator end() const
    {
        return ConstIterator(this, null_block_index);
    }

    private:

    static constexpr uint8_t significand_bits = 4;
    static constexpr uint8_t exponent_bits = 5;
    static constexpr uint8_t second_level_bins = 1 << significand_bits;
    static constexpr uint8_t first_level_bins = 1 << exponent_bits;
    static constexpr uint8_t max_significand_value = (2 << significand_bits) - 1;
    static constexpr uint8_t max_exponent_value = (1 << exponent_bits) - 1;
    static constexpr uint64_t max_allocation_size = max_significand_value << max_exponent_value;

    uint32_t capacity = 0;
    uint32_t size = 0;

    uint32_t first_level_bitmap = 0;
    uint16_t second_level_bitmaps[first_level_bins] = {};

    NodeIndex free_lists[first_level_bins][second_level_bins] = {};
    
    NodeIndex first_block = null_block_index;

    Pool<Block> blocks;

    uint8_t FirstLevelIndex(uint32_t size);
    uint8_t SecondLevelIndex(uint32_t size, uint32_t first_level_index);
    NodeIndex GetGoodFitBlock(uint32_t size);
    void InsertAfter(NodeIndex block, NodeIndex new_block);
    void InsertBefore(NodeIndex block, NodeIndex new_block);
    void RemoveBlock(NodeIndex block);
    void InsertFreeBlock(NodeIndex block);
    void RemoveFreeBlock(NodeIndex block);
};