#pragma once

#include <directx/d3d12.h>
#include <glm/glm.hpp>
#include <wrl/client.h>

#include "BufferAllocator.h"
#include "Config.h"
#include "MultiBuffer.h"

class RaytracingAccelerationStructure {

    public:

    struct Blas {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    };

    struct DynamicBlas {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint64_t update_scratch_size;
    };
    
    void Init(ID3D12Device5* device, uint32_t max_blas_vertices, uint32_t max_tlas_instances);
    
    void BuildStaticBlas(ID3D12GraphicsCommandList4* command_list, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, Blas* blas);
    void BuildDynamicBlas(ID3D12GraphicsCommandList4* command_list, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, DynamicBlas* blas);
    void UpdateDynamicBlas(ID3D12GraphicsCommandList4* command_list, DynamicBlas* blas, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices);
    void EndBlasBuilds(ID3D12GraphicsCommandList4* command_list);
    
    void BeginTlasBuild();
    bool AddTlasInstance(const Blas* blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags);
    bool AddTlasInstance(const DynamicBlas* blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags);
    void BuildTlas(ID3D12GraphicsCommandList4* command_list);
    
    D3D12_GPU_VIRTUAL_ADDRESS GetAccelerationStructure();
    
    private:
    
    Microsoft::WRL::ComPtr<ID3D12Device5> device;

    uint64_t max_blas_scratch_size = 0;
    LinearBuffer blas_scratch;

    uint32_t instance_count = 0;
    uint32_t max_tlas_instances = 0;
    MultiBuffer<CpuMappedLinearBuffer, Config::FRAME_COUNT> tlas_staging;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas_scratch;
    Microsoft::WRL::ComPtr<ID3D12Resource> tlas;

    void BuildBlas(ID3D12GraphicsCommandList4* command_list, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, ID3D12Resource** resource, uint64_t* update_scratch_size = nullptr);
    bool AddTlasInstance(D3D12_GPU_VIRTUAL_ADDRESS blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags);
};