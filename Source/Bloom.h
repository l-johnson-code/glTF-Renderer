#pragma once

#include <directx/d3d12.h>

#include "CommandContext.h"
#include "GpuResources.h"

class Bloom {

    public:

    void Create(ID3D12Device* device, GpuResources* resources, uint16_t width, uint16_t height, uint8_t max_iterations);
    void Resize(uint16_t width, uint16_t height, uint8_t max_iterations);
    void Execute(CommandContext* context, GpuResources::RenderTarget* input, D3D12_RESOURCE_STATES input_resource_states, uint8_t iterations, float strength);

    private:

    uint8_t max_iterations = 0;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    GpuResources* resources;
    GpuResources::Texture mip_chain;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> downsample_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> upsample_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;

};