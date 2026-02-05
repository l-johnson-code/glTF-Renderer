#pragma once

#include <wrl/client.h>

#include "CommandContext.h"

class ToneMapper {

	public:

	enum Tonemapper {
		TONEMAPPER_NONE,
		TONEMAPPER_AGX,
	};

	struct Config {
		int tonemapper = TONEMAPPER_AGX;
		float exposure = 1.0;
		int frame;
	};

    void Create(ID3D12Device* device);
    void Run(CommandContext* context, D3D12_GPU_DESCRIPTOR_HANDLE input_descriptor, const Config* config);

	private:

	enum OutputRootParameter {
        ROOT_PARAMETER_INPUT,
		ROOT_PARAMETER_CONFIG,
		ROOT_PARAMETER_COUNT,
	};

    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state;
};