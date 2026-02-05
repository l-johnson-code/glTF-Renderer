#pragma once

#include <directx/d3d12.h>

#include "CommandContext.h"

class Bloom {

    public:

    void Create(ID3D12Device* device, uint32_t width, uint32_t height, int max_iterations);
    void Resize(uint32_t width, uint32_t height, int max_iterations);
    void Execute(CommandContext* context, ID3D12Resource* input, D3D12_RESOURCE_STATES input_resource_states, int iterations, float strength);

    private:

    int max_iterations = 0;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    Microsoft::WRL::ComPtr<ID3D12Resource> mip_chain;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> downsample_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> upsample_pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;

};