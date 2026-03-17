#include "GpuAllocator.h"

#include <dxgi1_4.h>

#include "Profiling.h"
#include "DirectXHelpers.h"

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

HRESULT GpuAllocator::Init(ID3D12Device* device)
{
    this->device = device;

    // Check for GPU upload heap support.
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 options = {};
    HRESULT result = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &options, sizeof(options));
    if (FAILED(result)) {
        DeInit();
        return result;
    }
    this->supports_gpu_upload_heap = SUCCEEDED(result) && options.GPUUploadHeapSupported;

    // Check if UMA.
    D3D12_FEATURE_DATA_ARCHITECTURE architecture = {};
    result = this->device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &architecture, sizeof(architecture));
    if (FAILED(result)) {
        DeInit();
        return result;
    }
    bool is_uma = (bool)architecture.UMA;

    // Get memory info.
    LUID luid = this->device->GetAdapterLuid();
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    result = CreateDXGIFactory(IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        DeInit();
        return result;
    }
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
    result = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
    if (FAILED(result)) {
        DeInit();
        return result;
    }
    
    // Get current budget.
    DXGI_QUERY_VIDEO_MEMORY_INFO local_info = {};
    result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local_info);
    if (FAILED(result)) {
        DeInit();
        return result;
    }
    this->local_budget = local_info.Budget;
    DXGI_QUERY_VIDEO_MEMORY_INFO non_local_info = {};
    result = adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &non_local_info);
    if (FAILED(result)) {
        DeInit();
        return result;
    }
    this->non_local_budget = non_local_info.Budget;

    // Get capacity.
    DXGI_ADAPTER_DESC adapter_desc = {};
    result = adapter->GetDesc(&adapter_desc);
    if (FAILED(result)) {
        DeInit();
        return result;
    }
    this->local_capacity = is_uma ? adapter_desc.DedicatedSystemMemory + adapter_desc.SharedSystemMemory : adapter_desc.DedicatedVideoMemory;
    this->non_local_capacity = is_uma ? 0 : adapter_desc.SharedSystemMemory;
    return S_OK;
}

void GpuAllocator::DeInit()
{
    this->device.Reset();
    this->heaps = std::vector<TlsfHeap>();
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