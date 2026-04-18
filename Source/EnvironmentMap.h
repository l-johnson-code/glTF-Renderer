#pragma once

#include <wrl.h>

#include <directx/d3d12.h>
#include <glm/glm.hpp>

#include "CommandContext.h"
#include "DescriptorAllocator.h"
#include "UploadBuffer.h"

class EnvironmentMap {

    public:

    struct Map {
        int cube_srv_descriptor = -1;
        int ggx_srv_descriptor = -1;
        int diffuse_srv_descriptor = -1;
        int alias_srv_descriptor = -1;
        int pdf_srv_descriptor = -1;
        GpuResource cube;
        GpuResource ggx;
        GpuResource diffuse;
        GpuResource alias;
        GpuResource pdf;
    };
    
    GpuResource equirectangular_image;
    
    static float MipToRoughness(int mip_level, int mip_count);
    void Init(ID3D12Device* device, GpuAllocator* allocator, CbvSrvUavPool* descriptor_allocator);
    // Note: Loading the initial image and processing the image into cubemaps are separated so that they can happen at different times.
    void LoadEnvironmentMapImage(UploadBuffer* upload_buffer, const char* filepath, Map* map);
    void CreateEnvironmentMap(CommandContext* context, ID3D12Resource* equirectangular_image, Map* map);
    void DestroyEnvironmentMap(Map* map);
    
    private:
    
    enum Bsdf {
        BSDF_DIFFUSE,
        BSDF_GGX,
    };

    struct AliasMap {
        float prob;
        glm::u16vec2 pixel;
        glm::u16vec2 alias;
    };
    
    ID3D12Device* device = nullptr;
    GpuAllocator* allocator = nullptr;
    CbvSrvUavPool* descriptor_allocator = nullptr;
    
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> generate_cubemap_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> generate_cube_mip_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> generate_importance_map_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> generate_importance_map_level_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filter_cube_map_pipeline_state;
    
    void LoadEnvironmentMapImageExr(UploadBuffer* upload_buffer, const char* filepath, Map* map);
    void LoadEnvironmentMapImageHdr(UploadBuffer* upload_buffer, const char* filepath, Map* map);
    void GenerateCubemap(CommandContext* context, ID3D12Resource* equirectangular_image, ID3D12Resource* cubemap);
    void FilterCube(CommandContext* context, int cubemap_srv_descriptor, Bsdf bsdf, float mip_bias, int num_of_samples, ID3D12Resource* filtered_cube_map);
    void GenerateGgxCube(CommandContext* context, int cubemap_srv_descriptor, ID3D12Resource* ggx_cube_map);
    void GenerateDiffuseCube(CommandContext* context, int cubemap_srv_descriptor, ID3D12Resource* diffuse_cube_map);
    void GenerateAliasTable(UploadBuffer* upload, Map* map, int width, int height, float* data);
};