#include "DescriptorAllocator.h"

#include <bit>

HRESULT DescriptorAllocator::Create(int capacity)
{
    // TODO: Handle capacity of 0.
    for (int i = 0; i < std::size(free_lists); i++) {
        free_lists[i] = 0xffff;
    }
    // Round up size to next multiple of 64.
    blocks = std::vector<Block>((capacity + 63) / 64);
    free_lists[6] = 0;
    blocks[0].previous_free = 0xffff;
    for (int i = 1; i < blocks.size(); i++) {
        blocks[i].previous_free = i - 1;
        blocks[i - 1].next_free = i;
    }
    return S_OK;
};

void DescriptorAllocator::Destroy()
{
    blocks = std::vector<Block>();
    size = 0;
}

int DescriptorAllocator::Allocate(int count)
{
    assert((count > 0) && (count <= 64));
    int size_class = std::bit_width((uint32_t)count - 1);
    if (free_lists[size_class] != 0xffff) {
        size += 1 << size_class;
        return AllocateInBlock(free_lists[size_class]) + descriptor_start;
    }
    if (free_lists[6] != 0xffff) {
        int block_index = free_lists[6];
        RemoveFreeBlock(block_index);
        ResetBlock(block_index, size_class);
        AddFreeBlock(block_index);
        size += 1 << size_class;
        return AllocateInBlock(block_index) + descriptor_start;
    }
    return -1;
}

void DescriptorAllocator::Free(int descriptor)
{
    if (descriptor == -1) {
        return;
    }
    descriptor -= descriptor_start;
    int block_index = descriptor / 64;
    FreeInBlock(block_index, descriptor);
}

int DescriptorAllocator::Capacity()
{
    return blocks.size() * 64;
}

int DescriptorAllocator::Size()
{
    return size;
}

int DescriptorAllocator::AllocateInBlock(int block_index)
{
    Block& block = blocks[block_index];
    int descriptor = std::countr_zero(block.free_slots);
    block.free_slots &= ~((uint64_t)1 << (uint64_t)descriptor);

    // Remove block from free lists if its full.
    if (block.free_slots == 0) {
        RemoveFreeBlock(block_index);  
    }

    descriptor = block_index * 64 + descriptor * (1 << block.size_class);
    return descriptor;
}

uint64_t DescriptorAllocator::FreeMask(uint8_t size_class)
{
    return std::numeric_limits<uint64_t>::max() >> (uint64_t)(64 - 64 / (1 << size_class));
}

void DescriptorAllocator::RemoveFreeBlock(int block_index)
{
    Block& block = blocks[block_index];
    if (block.next_free != 0xffff) {
        blocks[block.next_free].previous_free = block.previous_free;
    }
    if (block.previous_free != 0xffff) {
        blocks[block.previous_free].next_free = block.next_free;
    }
    if (free_lists[block.size_class] == block_index) {
        free_lists[block.size_class] = block.next_free;
    }
    block.next_free = block.previous_free = 0xffff;
}

void DescriptorAllocator::AddFreeBlock(int block_index)
{
    Block& block = blocks[block_index];
    block.next_free = free_lists[block.size_class];
    if (block.next_free != 0xffff) {
        blocks[block.next_free].previous_free = block_index;
    }
    block.previous_free = 0xffff;
    free_lists[block.size_class] = block_index;
}

void DescriptorAllocator::ResetBlock(int block_index, uint8_t size_class)
{
    blocks[block_index] = {
        .size_class = size_class,
        .next_free = 0xffff,
        .previous_free = 0xffff,
        .free_slots = FreeMask(size_class),
    };
}

void DescriptorAllocator::FreeInBlock(int block_index, int descriptor)
{
    Block& block = blocks[block_index];
    descriptor %= 64;
    descriptor /= (1 << block.size_class); // TODO: Consider replacing division with bit shift.
    assert(!(block.free_slots & ((uint64_t)1 << (uint64_t)descriptor)) && "Possible double free.");
    block.free_slots |= (uint64_t)1 << (uint64_t)descriptor;
    size -= 1 << block.size_class;

    // Add block to free lists.
    if (block.free_slots == FreeMask(block.size_class)) {
        RemoveFreeBlock(block_index);
        ResetBlock(block_index, 6);
        AddFreeBlock(block_index);
    } else if (std::popcount(block.free_slots) == 1) {
        AddFreeBlock(block_index);
    }
}

void TargetPoolBase::Destroy()
{
    this->device.Reset();
    this->descriptor_heap.Reset();
}

int TargetPoolBase::Size()
{
    return std::popcount(free_slots);
}

int TargetPoolBase::Capacity()
{
    return 64;
}

void TargetPoolBase::FreeDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE descriptor)
{
    if (descriptor.ptr == 0) {
        return;
    }
    assert(
        (descriptor.ptr >= start.ptr) &&
        (descriptor.ptr < start.ptr + 64 * stride) &&
        (((descriptor.ptr - start.ptr) % stride) == 0) &&
        "Descriptor does not belong to this pool."
    );
    int index = (descriptor.ptr - start.ptr) / stride;
    free_slots |= 1 << index;
}

HRESULT TargetPoolBase::Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {
        .Type = type,
        .NumDescriptors = 64,
    };
    HRESULT result = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap));
    if (FAILED(result)) {
        Destroy();
        return result;
    }

    this->device = device;
    this->free_slots = std::numeric_limits<uint64_t>::max();
    this->stride = device->GetDescriptorHandleIncrementSize(type);
    this->start = descriptor_heap->GetCPUDescriptorHandleForHeapStart();

    return S_OK;
}

D3D12_CPU_DESCRIPTOR_HANDLE TargetPoolBase::AllocateDescriptor()
{
    if (free_slots == 0) {
        return {0}; 
    }
    int index = std::countr_zero(free_slots);
    free_slots &= ~(1 << index);
    return {start.ptr + index * stride};
}

HRESULT RenderTargetViewPool::Create(ID3D12Device* device)
{
    return TargetPoolBase::Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetViewPool::CreateRenderTargetView(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* desc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = AllocateDescriptor();
    if (descriptor.ptr != 0) {
        device->CreateRenderTargetView(resource, desc, descriptor);
    }
    return descriptor;
}

HRESULT DepthStencilViewPool::Create(ID3D12Device* device)
{
    return TargetPoolBase::Create(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
}

D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilViewPool::CreateDepthStencilView(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc)
{
    D3D12_CPU_DESCRIPTOR_HANDLE descriptor = AllocateDescriptor();
    if (descriptor.ptr != 0) {
        device->CreateDepthStencilView(resource, desc, descriptor);
    }
    return descriptor;
}