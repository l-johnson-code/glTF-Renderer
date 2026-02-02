#pragma once

#include <cassert>
#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

template<D3D12_DESCRIPTOR_HEAP_TYPE heap_type>
class DescriptorRange {
    public:
    HRESULT Create(ID3D12Device* device, int capacity, bool gpu_visible) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {
            .Type = heap_type,
            .NumDescriptors = (uint32_t)capacity,
            .Flags = gpu_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            .NodeMask = 1,
        };
        return Create(device, &desc);
    }

    HRESULT Create(ID3D12Device* device, int capacity) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {
            .Type = heap_type,
            .NumDescriptors = (uint32_t)capacity,
            .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
            .NodeMask = 1,
        };
        return Create(device, &desc);
    }

    void Create(const DescriptorRange<heap_type>* descriptor_range, int start, int capacity)
    {
        // TODO: Return an error if start + capacity is past the end of the descriptor range.
        this->device = descriptor_range->device;
        this->descriptor_heap = descriptor_range->descriptor_heap;
        this->descriptor_start = start;
        this->capacity = capacity;
        this->stride = descriptor_range->stride;
        this->cpu_start = descriptor_range->cpu_start;
        this->gpu_start = descriptor_range->gpu_start;
    }

    void CreateCbv(int index, const D3D12_CONSTANT_BUFFER_VIEW_DESC* cbv_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        assert(IsWithinBounds(index));
        device->CreateConstantBufferView(cbv_desc, GetCpuHandle(index));
    }

    void CreateSrv(int index, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        assert(IsWithinBounds(index));
        device->CreateShaderResourceView(resource, srv_desc, GetCpuHandle(index));
    }

    void CreateUav(int index, ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        assert(IsWithinBounds(index));
        device->CreateUnorderedAccessView(resource, nullptr, uav_desc, GetCpuHandle(index));
    }

    void CreateSampler(int index, const D3D12_SAMPLER_DESC* sampler_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        assert(IsWithinBounds(index));
        device->CreateSampler(sampler_desc, GetCpuHandle(index));
    }

    void CreateRtv(int index, ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* rtv_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
    {
        assert(IsWithinBounds(index));
        device->CreateRenderTargetView(resource, rtv_desc, GetCpuHandle(index));
    }

    void CreateDsv(int index, ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* dsv_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    {
        assert(IsWithinBounds(index));
        device->CreateDepthStencilView(resource, dsv_desc, GetCpuHandle(index));
    }
    
    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(int index) const
    {
        if (index == -1) {
            return {0};
        }
        D3D12_CPU_DESCRIPTOR_HANDLE result = this->cpu_start;
        result.ptr += index * this->stride;
        return result;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(int index) const
    {
        assert(gpu_start.ptr != 0);
        if (index == -1) {
            return {0};
        }
        D3D12_GPU_DESCRIPTOR_HANDLE result = this->gpu_start;
        result.ptr += index * this->stride;
        return result;
    }

    int GetIndex(D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle) const
    {
        assert(((cpu_handle.ptr - this->cpu_start.ptr) % this->stride) == 0);
        int index = ((cpu_handle.ptr - this->cpu_start.ptr) / this->stride);
        assert(index >= this->descriptor_start);
        assert(index < (this->descriptor_start + this->capacity));
        return index;
    }

    int GetIndex(D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle) const
    {
        assert(((gpu_handle.ptr - this->gpu_start.ptr) % this->stride) == 0);
        int index = ((gpu_handle.ptr - this->gpu_start.ptr) / this->stride);
        assert(index >= this->descriptor_start);
        assert(index < (this->descriptor_start + this->capacity));
        return index;
    }

    int GetAbsoluteIndex(int relative_index) const
    {
        return relative_index + this->descriptor_start;
    }

    int GetRelativeIndex(int absolute_index) const
    {
        return absolute_index - this->descriptor_start;
    }

    int Capacity() const
    {
        return this->capacity;
    }

    ID3D12DescriptorHeap* DescriptorHeap()
    {
        return this->descriptor_heap.Get();
    }

    void Destroy()
    {
        descriptor_heap.Reset();
        device.Reset();
    }

    protected:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptor_heap;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    int descriptor_start = 0;
    int capacity = 0;
    uint32_t stride = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_start = {};

    bool IsWithinBounds(int index) const
    {
        return index < (this->descriptor_start + this->capacity);
    }

    private:

    HRESULT Create(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_DESC* desc)
    {
        HRESULT result = S_OK;

        result = device->CreateDescriptorHeap(desc, IID_PPV_ARGS(this->descriptor_heap.ReleaseAndGetAddressOf()));
        if (result != S_OK) {
            Destroy();
            return result;
        }

        this->device = device;
        this->descriptor_start = 0;
        this->capacity = desc->NumDescriptors;
        this->stride = this->device->GetDescriptorHandleIncrementSize(heap_type);
        this->cpu_start = this->descriptor_heap->GetCPUDescriptorHandleForHeapStart();
        if (desc->Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
            this->gpu_start = this->descriptor_heap->GetGPUDescriptorHandleForHeapStart();
        } else {
            this->gpu_start = {0};
        }

        return S_OK;
    }
};

template<D3D12_DESCRIPTOR_HEAP_TYPE heap_type>
class DescriptorStack : public DescriptorRange<heap_type> {
    
    public:
    HRESULT Create(ID3D12Device* device, int capacity, bool gpu_visible) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        this->size = 0;
        return DescriptorRange<heap_type>::Create(device, capacity, gpu_visible);
    }

    HRESULT Create(ID3D12Device* device, int capacity) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    {
        this->size = 0;
        return DescriptorRange<heap_type>::Create(device, capacity);
    }

    void Create(DescriptorRange<heap_type>* descriptor_range, int start, int capacity)
    {
        this->size = 0;
        DescriptorRange<heap_type>::Create(descriptor_range, start, capacity);
    }

    int Allocate(int num_of_descriptors)
    {
        int new_size = this->size + num_of_descriptors;
        if (new_size > DescriptorRange<heap_type>::Capacity()) {
            return -1;
        }
        int index = this->descriptor_start + this->size;
        this->size = new_size;
        return index;
    }

    void Reset()
    {
        this->size = 0;
    }

    int Size() const
    {
        return this->size;
    }
    
    protected:
    int size = 0;
};

template<D3D12_DESCRIPTOR_HEAP_TYPE heap_type>
class DescriptorPool : public DescriptorRange<heap_type> {
    
    public:
    HRESULT Create(ID3D12Device* device, int capacity, bool gpu_visible) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        HRESULT result = DescriptorRange<heap_type>::Create(device, capacity, gpu_visible);
        if (result != S_OK) {
            Destroy();
            return result;
        }
        Reset();
        return S_OK;
    }

    HRESULT Create(ID3D12Device* device, int capacity) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    {
        HRESULT result = DescriptorRange<heap_type>::Create(device, capacity);
        if (result != S_OK) {
            Destroy();
            return result;
        }
        Reset();
        return S_OK;
    }

    void Create(DescriptorRange<heap_type>* descriptor_range, int start, int capacity)
    {
        DescriptorRange<heap_type>::Create(descriptor_range, start, capacity);
        Reset();
    }

    int Allocate()
    {
        if (this->free_descriptors.empty()) {
            return -1;
        }
        this->size++;
        int descriptor = free_descriptors.back();
        this->free_descriptors.pop_back();
        #ifdef DEBUG
        assert(this->used_descriptors[descriptor] == false);
        this->used_descriptors[descriptor] = true;
        #endif
        return descriptor;
    }

    int AllocateAndCreateCbv(const D3D12_CONSTANT_BUFFER_VIEW_DESC* cbv_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        int descriptor = Allocate();
        if (descriptor != -1) {
            DescriptorRange<heap_type>::CreateCbv(descriptor, cbv_desc);
        }
        return descriptor;
    }

    int AllocateAndCreateSrv(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* srv_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        int descriptor = Allocate();
        if (descriptor != -1) {
            DescriptorRange<heap_type>::CreateSrv(descriptor, resource, srv_desc);
        }
        return descriptor;
    }

    int AllocateAndCreateUav(ID3D12Resource* resource, ID3D12Resource* counter_resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* uav_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        int descriptor = Allocate();
        if (descriptor != -1) {
            DescriptorRange<heap_type>::CreateUav(descriptor, resource, counter_resource, uav_desc);
        }
        return descriptor;
    }

    int AllocateAndCreateSampler(int index, const D3D12_SAMPLER_DESC* sampler_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
    {
        int descriptor = Allocate();
        if (descriptor != -1) {
            DescriptorRange<heap_type>::CreateSampler(descriptor, sampler_desc);
        }
        return descriptor;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE AllocateAndCreateRtv(ID3D12Resource* resource, const D3D12_RENDER_TARGET_VIEW_DESC* rtv_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
    {
        int descriptor = Allocate();
        if (descriptor != -1) {
            DescriptorRange<heap_type>::CreateRtv(descriptor, resource, rtv_desc);
        }
        return DescriptorRange<heap_type>::GetCpuHandle(descriptor);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE AllocateAndCreateDsv(ID3D12Resource* resource, const D3D12_DEPTH_STENCIL_VIEW_DESC* dsv_desc) requires (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV)
    {
        int descriptor = Allocate();
        if (descriptor != -1) {
            DescriptorRange<heap_type>::CreateDsv(descriptor, resource, dsv_desc);
        }
        return DescriptorRange<heap_type>::GetCpuHandle(descriptor);
    }
    
    void Free(int index)
    {
        if (index != -1) {
            assert((index >= this->descriptor_start) && (index < (this->descriptor_start + this->capacity)));
            assert(this->size != 0);
            this->size--;
            this->free_descriptors.push_back(index);
            #ifdef DEBUG
            assert(this->used_descriptors[index] == true); // Double free.
            this->used_descriptors[index] = false;
            #endif
        }
    }

    void Free(D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        Free(DescriptorRange<heap_type>::GetIndex(handle));
    }

    void Reset()
    {
        this->size = 0;
        this->free_descriptors.clear();
        for (int i = this->descriptor_start + this->capacity; (i--) > this->descriptor_start;) {
            this->free_descriptors.push_back(i);
        }
        #ifdef DEBUG
        used_descriptors = std::vector<bool>(this->capacity, false);
        #endif
    }

    int Size() const
    {
        return size;
    }

    void Destroy()
    {
        this->size = 0;
        this->free_descriptors = std::vector<int>();
        #ifdef DEBUG
        this->used_descriptors = std::vector<bool>();
        #endif
        DescriptorRange<heap_type>::Destroy();
    }
    
	protected:
    int size = 0;
    
	private:
	std::vector<int> free_descriptors;
    #ifdef DEBUG
    std::vector<bool> used_descriptors;
    #endif
};

using CbvSrvUavRange = DescriptorRange<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV>;
using SamplerRange = DescriptorRange<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER>;
using RtvRange = DescriptorRange<D3D12_DESCRIPTOR_HEAP_TYPE_RTV>;
using DsvRange = DescriptorRange<D3D12_DESCRIPTOR_HEAP_TYPE_DSV>;

using CbvSrvUavStack = DescriptorStack<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV>;
using SamplerStack = DescriptorStack<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER>;
using RtvStack = DescriptorStack<D3D12_DESCRIPTOR_HEAP_TYPE_RTV>;
using DsvStack = DescriptorStack<D3D12_DESCRIPTOR_HEAP_TYPE_DSV>;

using CbvSrvUavPool = DescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV>;
using SamplerPool = DescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER>;
using RtvPool = DescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_RTV>;
using DsvPool = DescriptorPool<D3D12_DESCRIPTOR_HEAP_TYPE_DSV>;