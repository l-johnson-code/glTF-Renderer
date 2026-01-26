#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>

#include "GpuResources.h"
#include "BufferAllocator.h"
#include "Mesh.h"

class ForwardPass {
    public:

    enum RenderFlags {
        RENDER_FLAG_ENVIRONMENT = 1 << 0,
        RENDER_FLAG_POINT_LIGHTS = 1 << 1,
    };

    enum PipelineFlags {
        PIPELINE_FLAGS_NONE = 0,
        PIPELINE_FLAGS_DOUBLE_SIDED = 1 << 0,
        PIPELINE_FLAGS_WINDING_ORDER_CLOCKWISE = 1 << 1,
        PIPELINE_FLAGS_ALPHA_BLEND = 1 << 2,
        PIPELINE_FLAGS_PERMUTATION_COUNT = 1 << 3,
        PIPELINE_FLAGS_BITMASK = PIPELINE_FLAGS_PERMUTATION_COUNT - 1,
    };

    static constexpr int TRANSMISSION_DOWNSAMPLE_SAMPLE_PATTERN_COUNT = 3;

    struct Config {
        int width;
        int height;
        glm::vec2 jitter;
        glm::vec2 previous_jitter;
		glm::mat4x4 world_to_clip;
		glm::mat4x4 previous_world_to_clip;
        glm::vec3 camera_pos;
        int num_of_lights;
        D3D12_GPU_VIRTUAL_ADDRESS lights;
        D3D12_GPU_VIRTUAL_ADDRESS materials;
        int ggx_cube_descriptor;
        int diffuse_cube_descriptor;
        float environment_map_intensity;
        int transmission_descriptor = -1;
        uint32_t render_flags;
    };

    void Create(ID3D12Device* device);
    void Destroy();
    void SetRootSignature(ID3D12GraphicsCommandList* command_list);
    void SetConfig(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, const Config* config);
    void BindRenderTargets(ID3D12GraphicsCommandList* command_list, GpuResources* resources, D3D12_CPU_DESCRIPTOR_HANDLE render, D3D12_CPU_DESCRIPTOR_HANDLE velocity);
    void BindPipeline(ID3D12GraphicsCommandList* command_list, GpuResources* resources, uint32_t pipeline_flags);
    void Draw(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, Mesh* model, int material_id, glm::mat4x4 model_to_world, glm::mat4x4 model_to_world_normals, glm::mat4x4 previous_model_to_world, DynamicMesh* dynamic_mesh = nullptr);
    void DrawBackground(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, glm::mat4x4 clip_to_world, float environment_intensity, int environment_descriptor);
    void GenerateTransmissionMips(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, DescriptorStack* transient_descriptors, ID3D12Resource* input, ID3D12Resource* output, int sample_pattern);

    private:

    enum RootParameterIndex {
		ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME,
		ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_MODEL,
		ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_FRAME,
		ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_MODEL,
		ROOT_PARAMETER_SRV_LIGHTS,
		ROOT_PARAMETER_SRV_MATERIALS,
		ROOT_PARAMETER_COUNT,
	};

    D3D12_PRIMITIVE_TOPOLOGY current_topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    uint32_t current_pipeline_flags;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_states[PIPELINE_FLAGS_PERMUTATION_COUNT];

    Microsoft::WRL::ComPtr<ID3D12RootSignature> background_root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> background_pipeline_state;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> transmission_mips_root_signature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> transmission_mips_pipeline_state;

    void CreatePipeline(ID3D12Device* device, D3D12_SHADER_BYTECODE vertex_shader, D3D12_SHADER_BYTECODE pixel_shader, uint32_t flags, ID3D12RootSignature* root_signature);
    void CreateBackgroundRenderer(ID3D12Device* device);
    void CreateTranmissionMipPipeline(ID3D12Device* device);
};