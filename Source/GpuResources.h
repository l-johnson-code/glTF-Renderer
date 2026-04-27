#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Config.h"
#include "DescriptorAllocator.h"
#include "MultiBuffer.h"
#include "UploadBuffer.h"
#include "GpuAllocator.h"

class GpuResources {
    public:

	enum StaticDescriptor {
		STATIC_DESCRIPTOR_SRV_SHEEN_E,
		STATIC_DESCRIPTOR_COUNT,
	};

	enum TextureFlags : uint8_t {
		TEXTURE_FLAG_NONE = 0,
		TEXTURE_FLAG_SRV = 1 << 0,
		TEXTURE_FLAG_UAV = 1 << 1,
		TEXTURE_FLAG_RENDER_TARGET = 1 << 2,
		TEXTURE_FLAG_DEPTH_TARGET = 1 << 3,
		TEXTURE_FLAG_CUBE = 1 << 4,
		TEXTURE_FLAG_SRV_PER_MIP = 1 << 5,
	};

	struct TextureDesc {
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		uint16_t width = 0;
		uint16_t height = 0;
		uint8_t mip_levels = 0;
		union {
			float clear_depth;
			float clear_color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		};
		TextureFlags flags = TEXTURE_FLAG_NONE;
		D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
		const char* name;
	};

	struct Texture {
		GpuResource resource;
		uint16_t width = 0;
		uint16_t height = 0;
		uint8_t mip_levels = 0;
		int srv = -1;
		int uav = -1;
		union {
			struct {
				D3D12_CPU_DESCRIPTOR_HANDLE dsv;
				float clear_depth;
			} depth;
			struct {
				D3D12_CPU_DESCRIPTOR_HANDLE rtv;
				float clear_color[4]; 
			} render = { .rtv = {0}, .clear_color = {0.0f, 0.0f, 0.0f, 0.0f} };
		};
		TextureFlags flags;
	};

	// Global GPU visible descriptor heaps. All other GPU visible descriptor heaps are suballocated from these.
	CbvSrvUavStack cbv_uav_srv_allocator;
	SamplerStack sampler_allocator;

	// Descriptor heaps that are dynamic.
	DescriptorAllocator cbv_uav_srv_dynamic_allocator;
	SamplerStack gltf_sampler_allocator;

	// Render target and depth stencil views.
	DepthStencilViewPool dsv_allocator;
	RenderTargetViewPool rtv_allocator;

	// Per frame descriptor allocators.
	MultiBuffer<CbvSrvUavStack, Config::FRAME_COUNT> cbv_uav_srv_frame_allocators;

	GpuAllocator allocator;
	
	void Create(ID3D12Device* device);
	void LoadLookupTables(UploadBuffer* upload_buffer);
	HRESULT CreateTexture(const TextureDesc* desc, Texture* texture);
	void FreeTexture(Texture* texture);
	static HRESULT CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** pipeline_state, const char* name = nullptr);
	static HRESULT CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** pipeline_state, const char* name = nullptr);
	static HRESULT CreateRootSignature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC* desc, ID3D12RootSignature** root_signature, const char* name = nullptr);
	static D3D12_SHADER_BYTECODE LoadShader(const char* filepath);
	static void FreeShader(D3D12_SHADER_BYTECODE shader);
	
	private:

	Microsoft::WRL::ComPtr<ID3D12Device> device;

	// Lookup tables.
	GpuResource sheen_e;
};