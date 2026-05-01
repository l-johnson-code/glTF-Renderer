#pragma once

#include <cstdint>

#include <directx/d3d12.h>
#include <wrl/client.h>

#include "GpuResources.h"

class LinearBuffer {
    public:
    
    HRESULT Create(Gpu::Resources* resources, const Gpu::BufferDesc* buffer_desc);
    void Destroy(Gpu::Resources* resources);
    uint64_t Size();
    uint64_t Capacity();
    void Reset();
    D3D12_GPU_VIRTUAL_ADDRESS Allocate(uint64_t size, uint64_t alignment);
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress(uint64_t offset);
    Gpu::Buffer buffer;
    
    protected:
    
    uint64_t size = 0;
};

class CpuMappedLinearBuffer : public LinearBuffer {
    public:

    void* Allocate(uint64_t size, uint64_t alignment, D3D12_GPU_VIRTUAL_ADDRESS* gpu_address);
    D3D12_GPU_VIRTUAL_ADDRESS Copy(const void* data, uint64_t size, uint64_t alignment);
    template<typename T>
    D3D12_GPU_VIRTUAL_ADDRESS Copy(const T* data, uint64_t alignment) 
    {
        return Copy((const void*)data, sizeof(*data), alignment);
    }
    void* GetCpuAddress(uint64_t offset);
};

class CircularBuffer {
    public:

    HRESULT Create(Gpu::Resources* resources, const Gpu::BufferDesc* buffer_desc);
    uint64_t Allocate(uint64_t size, uint64_t alignment);
    uint64_t GetMarker();
    void Free(uint64_t marker);
    void Reset();
    void* GetCpuAddress(uint64_t offset);
    D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress(uint64_t offset);
    ID3D12Resource* Resource();
    uint64_t Size();
    uint64_t Capacity();
    void Destroy(Gpu::Resources* resources);

    private:
    
    Gpu::Buffer buffer;
    uint64_t write = 0;
    uint64_t size = 0;
};