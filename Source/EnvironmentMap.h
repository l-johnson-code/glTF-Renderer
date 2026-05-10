#pragma once

#include <wrl.h>

#include <directx/d3d12.h>
#include <glm/glm.hpp>

#include "CommandContext.h"
#include "GpuResources.h"
#include "UploadBuffer.h"

class EnvironmentMap {

    public:

    struct Map {
        Gpu::Texture cube;
        Gpu::Texture ggx;
        Gpu::Texture diffuse;
        Gpu::Buffer alias;
        Gpu::Texture pdf;
    };
    
    Gpu::Texture equirectangular_image;
    
    static float MipToRoughness(int mip_level, int mip_count);
    void Init(Gpu::Resources* resources);
    // Note: Loading the initial image and processing the image into cubemaps are separated so that they can happen at different times.
    void LoadEnvironmentMapImage(UploadBuffer* upload_buffer, const char* filepath, Map* map);
    void CreateEnvironmentMap(CommandContext* context, Gpu::Texture* equirectangular_image, Map* map);
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
        uint32_t pdf;
    };
    
    Gpu::Resources* resources = nullptr;
    
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> generate_cubemap_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> generate_cube_mip_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> generate_importance_map_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> generate_importance_map_level_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> filter_cube_map_pipeline_state;
    
    void LoadEnvironmentMapImageExr(UploadBuffer* upload_buffer, const char* filepath, Map* map);
    void LoadEnvironmentMapImageHdr(UploadBuffer* upload_buffer, const char* filepath, Map* map);
    void GenerateCubemap(CommandContext* context, Gpu::Texture* equirectangular_image, Gpu::Texture* cubemap);
    void FilterCube(CommandContext* context, Gpu::Texture* cubemap, Bsdf bsdf, float mip_bias, int num_of_samples, Gpu::Texture* filtered_cube_map);
    void GenerateGgxCube(CommandContext* context, Gpu::Texture* cubemap, Gpu::Texture* ggx_cube_map);
    void GenerateDiffuseCube(CommandContext* context, Gpu::Texture* cubemap, Gpu::Texture* diffuse_cube_map);
    void GenerateAliasTable(UploadBuffer* upload, Map* map, int width, int height, float* data);
};