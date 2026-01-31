#pragma once

#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

class DescriptorRange {
    public:
    void Create(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc);
    void Create(ID3D12DescriptorHeap* descriptor_heap, int start, int capacity);
    void Create(const DescriptorRange* descriptor_range, int start, int capacity);
    void CreateCbv(int index, const D3D12_CONSTANT_BUFFER_VIEW_DESC* cbv_desc);
    void CreateSrv(int index, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc);
    void CreateUav(int index, ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc);
    void CreateSampler(int index, const D3D12_SAMPLER_DESC* sampler_desc);
    void CreateRtv(int index, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* rtv_desc);
    void CreateDsv(int index, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* dsv_desc);
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(int index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(int index) const;
    int GetIndex(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle) const;
    int GetIndex(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) const;
    int GetAbsoluteIndex(int relative_index) const;
    int GetRelativeIndex(int abolute_index) const;
    int Capacity() const;
    ID3D12DescriptorHeap* DescriptorHeap();

    protected:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    int descriptor_start = 0;
    int capacity = 0;
    uint32_t stride = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {};

    bool IsWithinBounds(int index) const;
};

class DescriptorStack : public DescriptorRange {
    
    public:
    void Create(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc);
    void Create(ID3D12DescriptorHeap* descriptor_heap, int start, int capacity);
    void Create(DescriptorRange* descriptor_range, int start, int capacity);
    int Allocate(int num_of_descriptors);
    void Reset();
    int Size() const;
    
    protected:
    int size = 0;
};

class DescriptorPool : public DescriptorRange {

	public:
    void Create(ID3D12Device* device, const D3D12_DESCRIPTOR_HEAP_DESC* desc);
    void Create(ID3D12DescriptorHeap* descriptor_heap, int start, int capacity);
    void Create(DescriptorRange* descriptor_range, int start, int capacity);
    int Allocate();
    int AllocateAndCreateCbv(const D3D12_CONSTANT_BUFFER_VIEW_DESC* cbv_desc);
    int AllocateAndCreateSrv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc);
    int AllocateAndCreateUav(ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc);
    int AllocateAndCreateRtv(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* rtv_desc);
    int AllocateAndCreateDsv(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* dsv_desc);
    void Free(int index);
    void Reset();
    int Size() const;

	protected:
    int size = 0;

	private:
	std::vector<int> free_descriptors;
    #ifdef DEBUG
    std::vector<bool> used_descriptors;
    #endif
};