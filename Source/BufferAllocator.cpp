#include "BufferAllocator.h"

#include <cassert>
#include <limits>

#include <directx/d3dx12_core.h>

#include "DirectXHelpers.h"
#include "Memory.h"

HRESULT LinearBuffer::Create(ID3D12Device* device, uint64_t capacity, D3D12_HEAP_PROPERTIES* heap_properties, D3D12_RESOURCE_FLAGS resource_flags, D3D12_RESOURCE_STATES initial_resource_state, const char* name)
{
    this->capacity = capacity;
    this->size = 0;

	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(capacity, resource_flags);
	
    HRESULT result = device->CreateCommittedResource(
        heap_properties,
        D3D12_HEAP_FLAG_NONE,
        &resource_desc,
        initial_resource_state,
        nullptr,
        IID_PPV_ARGS(this->resource.ReleaseAndGetAddressOf())
	);
    if (FAILED(result)) {
        Destroy();
        return result;
    }

    if (SUCCEEDED(result) && name) {
        SetName(this->resource.Get(), name);
    }
    
    return result;
}

void LinearBuffer::Destroy()
{
	this->resource.Reset();
	this->capacity = 0;
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
	return this->capacity;
}

D3D12_GPU_VIRTUAL_ADDRESS LinearBuffer::Allocate(uint64_t size, uint64_t alignment)
{
	uint64_t aligned_address = Align(this->size, alignment);
	uint64_t new_size = aligned_address + size;
	// Bounds check.
	if (new_size > this->capacity) {
		return 0;
	} else {
		this->size = new_size;
		return this->resource->GetGPUVirtualAddress() + aligned_address;
	}
}

HRESULT CpuMappedLinearBuffer::Create(ID3D12Device* device, uint64_t capacity, bool use_gpu_upload_heap, const char* name)
{
	HRESULT result = S_OK;

    this->capacity = capacity;
    this->size = 0;

	// Check if GPU upload heaps are supported.
	if (use_gpu_upload_heap) {
		D3D12_FEATURE_DATA_D3D12_OPTIONS16 options = {};
		result = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options, sizeof(options));
		if (SUCCEEDED(result)) {
			use_gpu_upload_heap = options.GPUUploadHeapSupported;
		} else {
			use_gpu_upload_heap = false;
		}
	}

	CD3DX12_HEAP_PROPERTIES heap_properties(use_gpu_upload_heap ? D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_UPLOAD);

	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(capacity);

    result = device->CreateCommittedResource(
        &heap_properties,
        D3D12_HEAP_FLAG_NONE,
        &resource_desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(this->resource.ReleaseAndGetAddressOf())
	);
    if (FAILED(result)) {
        Destroy();
        return result;
    }

    if (SUCCEEDED(result) && name) {
	    SetName(this->resource.Get(), name);
	}

	result = this->resource->Map(0, nullptr, &this->pointer);
	if (FAILED(result)) {
        Destroy();
        return result;
    }

	return result;
}

void CpuMappedLinearBuffer::Destroy()
{
	LinearBuffer::Destroy();
    this->pointer = nullptr;
}

void* CpuMappedLinearBuffer::Allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS* gpu_address)
{
	uint64_t aligned_address = Align(this->size, alignment);
	uint64_t new_size = aligned_address + size;
	// Bounds check.
	if (new_size > this->capacity) {
		*gpu_address = 0;
		return nullptr;
	} else {
		*gpu_address = this->resource->GetGPUVirtualAddress() + aligned_address;
		this->size = new_size;
		return (char*)(this->pointer) + aligned_address;
	}
}

D3D12_GPU_VIRTUAL_ADDRESS CpuMappedLinearBuffer::Copy(const void* data, uint64_t size, uint64_t alignment)
{
	assert(this->pointer);
    void* ptr = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_ptr = 0;
    ptr = Allocate(size, alignment, &gpu_ptr);
    memcpy(ptr, data, size);
    return gpu_ptr;
}

HRESULT CircularBuffer::Create(ID3D12Device* device, uint64_t capacity, D3D12_HEAP_PROPERTIES* heap_properties, D3D12_RESOURCE_FLAGS resource_flags, const char* name)
{
    this->capacity = capacity;
    this->size = 0;

	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(capacity, resource_flags);
	
    HRESULT result = device->CreateCommittedResource(
        heap_properties,
        D3D12_HEAP_FLAG_NONE,
        &resource_desc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(this->resource.ReleaseAndGetAddressOf())
	);
    if (FAILED(result)) {
        Destroy();
        return result;
    }

    if (SUCCEEDED(result) && name) {
        SetName(this->resource.Get(), name);
    }

    // Map the resource for CPU access.
    this->ptr = nullptr;
    result = this->resource->Map(0, nullptr, &this->ptr);
    if (FAILED(result)) {
        Destroy();
        return result;
    }

    return result;
}

uint64_t CircularBuffer::Allocate(uint64_t size, uint64_t alignment)
{
    // Try to allocate at the end of the buffer.
    uint64_t aligned_write = Align(this->write, alignment);
    uint64_t new_write = aligned_write + size;
    uint64_t new_size = this->size + (new_write - this->write);
    if (new_size <= capacity && new_write <= capacity) {
        this->size = new_size;
        this->write = new_write % capacity;
        return aligned_write;
    } else if ((size + (this->capacity - this->write)) < this->capacity){
        // Try to allocate at the beginning of the buffer.
        this->size += size + (this->capacity - this->write);
        this->write = size;
        return 0;
    } else {
        // Buffer is full.
        return std::numeric_limits<uint64_t>::max();
    }
}

void* CircularBuffer::GetCpuAddress(uint64_t offset)
{
    return (std::byte*)ptr + offset;
}

uint64_t CircularBuffer::GetMarker()
{
    return this->write;
}

void CircularBuffer::Free(uint64_t marker)
{
    uint64_t new_size;
    if (marker <= this->write) {
        new_size = this->write - marker;
    } else {
        new_size = this->write + (this->capacity - marker);
    }
    assert(new_size <= this->size); // Check we haven't freed in the incorrect order.
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
    return this->capacity;
}

ID3D12Resource* CircularBuffer::Resource()
{
    return this->resource.Get();
}

void CircularBuffer::Destroy()
{
    this->resource.Reset();
    this->ptr = nullptr;
    this->write = 0;
    this->size = 0;
    this->capacity = 0;
}