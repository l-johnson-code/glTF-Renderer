#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>

#include "CommandContext.h"
#include "Mesh.h"

class GpuSkin {
    public:

    struct Bone {
        glm::mat4x4 transform;
        glm::mat4x4 inverse_transpose;
    };

    void Create(ID3D12Device* device);
    void Bind(CommandContext* context);
    void Run(CommandContext* context, Mesh* input, DynamicMesh* output, D3D12_GPU_VIRTUAL_ADDRESS bones, int num_of_morph_targets, MorphTarget** morph_targets, float* morph_weights);

    private:

    enum RootParameterIndex {
        ROOT_PARAMETER_CONSTANT_BUFFER,
		ROOT_PARAMETER_VERTEX_INPUT,
		ROOT_PARAMETER_NORMAL_INPUT,
		ROOT_PARAMETER_TANGENT_INPUT,
        ROOT_PARAMETER_SKIN,
        ROOT_PARAMETER_BONES,
		ROOT_PARAMETER_VERTEX_OUTPUT,
		ROOT_PARAMETER_NORMAL_OUTPUT,
		ROOT_PARAMETER_TANGENT_OUTPUT,
		ROOT_PARAMETER_COUNT,
	};

    const int THREAD_GROUP_SIZE = 64;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
};