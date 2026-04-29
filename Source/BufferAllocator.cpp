#include "BufferAllocator.h"

#include <cassert>
#include <limits>

#include <directx/d3dx12_core.h>

#include "Memory.h"

HRESULT LinearBuffer::Create(GpuResources* resources, const GpuResources::BufferDesc* buffer_desc)
{
    this->size = 0;

    HRESULT result = resources->CreateBuffer(buffer_desc, &this->buffer);
    if (FAILED(result)) {
        Destroy(resources);
        return result;
    }
    
    return result;
}

void LinearBuffer::Destroy(GpuResources* resources)
{
	resources->FreeBuffer(&this->buffer);
    this->size = 0;
}

void LinearBuffer::Reset()
{
    this->size = 0;
}

uint64_t LinearBuffer::Size()
{
	return this->size;
}

uint64_t LinearBuffer::Capacity()
{
	return this->buffer.Size();
}

D3D12_GPU_VIRTUAL_ADDRESS LinearBuffer::Allocate(uint64_t size, uint64_t alignment)
{
	uint64_t aligned_address = Align(this->size, alignment);
	uint64_t new_size = aligned_address + size;
	// Bounds check.
	if (new_size > this->Capacity()) {
		return 0;
	} else {
		this->size = new_size;
		return this->buffer.Resource()->GetGPUVirtualAddress() + aligned_address;
	}
}

void* CpuMappedLinearBuffer::Allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS* gpu_address)
{
	uint64_t aligned_address = Align(this->size, alignment);
	uint64_t new_size = aligned_address + size;
	// Bounds check.
	if (new_size > this->Capacity()) {
		*gpu_address = 0;
		return nullptr;
	} else {
		*gpu_address = this->buffer.Resource()->GetGPUVirtualAddress() + aligned_address;
		this->size = new_size;
		return (char*)(this->buffer.Pointer()) + aligned_address;
	}
}

D3D12_GPU_VIRTUAL_ADDRESS CpuMappedLinearBuffer::Copy(const void* data, uint64_t size, uint64_t alignment)
{
	assert(this->buffer.Pointer());
    void* ptr = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_ptr = 0;
    ptr = Allocate(size, alignment, &gpu_ptr);
    memcpy(ptr, data, size);
    return gpu_ptr;
}

HRESULT CircularBuffer::Create(GpuResources* resources, const GpuResources::BufferDesc* buffer_desc)
{
    this->size = 0;

    HRESULT result = resources->CreateBuffer(buffer_desc, &this->buffer);
    if (FAILED(result)) {
        Destroy(resources);
        return result;
    }

    return result;
}

uint64_t CircularBuffer::Allocate(uint64_t size, uint64_t alignment)
{
    // Try to allocate at the end of the buffer.
    uint64_t aligned_write = Align(this->write, alignment);
    uint64_t new_write = aligned_write + size;
    uint64_t new_size = this->size + new_write - this->write;
    if (new_size <= Capacity() && new_write <= Capacity()) {
        this->size = new_size;
        this->write = new_write % Capacity();
        return aligned_write;
    }
    
    // Try to allocate at the beginning of the buffer.
    new_size = this->size + size + Capacity() - this->write;
    if (new_size <= Capacity()) {
        this->size = new_size;
        this->write = size;
        return 0;
    }

    // Buffer is full.
    return std::numeric_limits<uint64_t>::max();
}

void* CircularBuffer::GetCpuAddress(uint64_t offset)
{
    return (std::byte*)buffer.Pointer() + offset;
}

uint64_t CircularBuffer::GetMarker()
{
    return this->write;
}

void CircularBuffer::Free(uint64_t marker)
{
    assert(marker < Capacity());
    uint64_t new_size;
    if (marker <= this->write) {
        new_size = this->write - marker;
    } else {
        new_size = this->write + Capacity() - marker;
    }
    assert(new_size <= this->size);  // Check we haven't freed in the incorrect order.
    this->size = new_size;
}

void CircularBuffer::Reset()
{
    this->size = 0;
    this->write = 0;
}

uint64_t CircularBuffer::Size()
{
    return this->size;
}

uint64_t CircularBuffer::Capacity()
{
    return this->buffer.Size();
}

ID3D12Resource* CircularBuffer::Resource()
{
    return this->buffer.Resource();
}

void CircularBuffer::Destroy(GpuResources* resources)
{
    resources->FreeBuffer(&this->buffer);
    this->write = 0;
    this->size = 0;
}