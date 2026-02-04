#pragma once

#include <cstdint>

#include <directx/d3d12.h>
#include <wrl/client.h>

class LinearBuffer {
    public:
    
    HRESULT Create(ID3D12Device* device, uint64_t capacity, D3D12_HEAP_PROPERTIES* heap_properties, D3D12_RESOURCE_FLAGS resource_flags, D3D12_RESOURCE_STATES initial_resource_state, const char* name = nullptr);
    void Destroy();
    uint64_t Size();
    uint64_t Capacity();
    void Reset();
    D3D12_GPU_VIRTUAL_ADDRESS Allocate(uint64_t size, uint64_t alignment);
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress(uint64_t offset);
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    
    protected:
    
    uint64_t capacity = 0;
    uint64_t size = 0;
};

class CpuMappedLinearBuffer : public LinearBuffer {
    public:

    HRESULT Create(ID3D12Device* device, uint64_t capacity, bool use_gpu_upload_heap, const char* name = nullptr);
    void Destroy();
    void* Allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS* gpu_address);
    D3D12_GPU_VIRTUAL_ADDRESS Copy(const void* data, uint64_t size, uint64_t alignment);
    template<typename T>
    D3D12_GPU_VIRTUAL_ADDRESS Copy(const T* data, uint64_t alignment) 
    {
        return Copy((const void*)data, sizeof(*data), alignment);
    }
    void* GetCpuAddress(uint64_t offset);

    private:

    void* pointer = nullptr;
};

class CircularBuffer {
    public:
    HRESULT Create(ID3D12Device* device, uint64_t capacity, D3D12_HEAP_PROPERTIES* heap_properties, D3D12_RESOURCE_FLAGS resource_flags, const char* name = nullptr);
    uint64_t Allocate(uint64_t size, uint64_t alignment);
    uint64_t GetMarker();
    void Free(uint64_t marker);
    void Reset();
    void* GetCpuAddress(uint64_t offset);
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress(uint64_t offset);
    ID3D12Resource* Resource();
    uint64_t Size();
    uint64_t Capacity();
    void Destroy();

    private:
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    void* ptr = nullptr;
    uint64_t write = 0;
    uint64_t size = 0;
    uint64_t capacity = 0;
};