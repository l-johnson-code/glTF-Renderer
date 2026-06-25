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

    void Create(Gpu::Resources* resources);
    void Run(CommandContext* context, int input_descriptor, const Config* config);

	private:

	Gpu::Resources* resources;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_state;
};