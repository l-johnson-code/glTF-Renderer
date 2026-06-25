#pragma once

#include <directx/d3d12.h>
#include <glm/glm.hpp>
#include <wrl/client.h>

#include "BufferAllocator.h"
#include "CommandContext.h"
#include "Config.h"
#include "GpuResources.h"
#include "MultiBuffer.h"

class RaytracingAccelerationStructure {

    public:

    struct Blas {
        Gpu::Buffer buffer;
    };

    struct DynamicBlas {
        Gpu::Buffer buffer;
        uint64_t update_scratch_size;
    };
    
    void Init(ID3D12Device5* device, Gpu::Resources* resources, uint32_t max_blas_vertices, uint32_t max_tlas_instances);
    
    void BuildStaticBlas(CommandContext* command_context, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, Blas* blas);
    void BuildDynamicBlas(CommandContext* command_context, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, DynamicBlas* blas);
    void UpdateDynamicBlas(CommandContext* command_context, DynamicBlas* blas, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices);
    void EndBlasBuilds(CommandContext* command_context);
    
    void BeginTlasBuild();
    bool AddTlasInstance(Blas* blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags);
    bool AddTlasInstance(DynamicBlas* blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags);
    void BuildTlas(CommandContext* command_context);
    
    Gpu::Buffer& GetAccelerationStructure();
    
    private:
    
    Gpu::Resources* resources;

    Microsoft::WRL::ComPtr<ID3D12Device5> device;

    uint64_t max_blas_scratch_size = 0;
    LinearBuffer blas_scratch;

    uint32_t instance_count = 0;
    uint32_t max_tlas_instances = 0;
    MultiBuffer<CpuMappedLinearBuffer, Config::FRAME_COUNT> tlas_staging;
    Gpu::Buffer tlas_scratch;
    Gpu::Buffer tlas;

    void BuildBlas(CommandContext* command_context, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, Gpu::Buffer* buffer, uint64_t* update_scratch_size = nullptr);
    bool AddTlasInstance(D3D12_GPU_VIRTUAL_ADDRESS blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags);
};