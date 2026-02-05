#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Config.h"
#include "DescriptorAllocator.h"
#include "MultiBuffer.h"
#include "UploadBuffer.h"

class GpuResources {
    public:

	enum StaticDescriptor {
		STATIC_DESCRIPTOR_SRV_SHEEN_E,
		STATIC_DESCRIPTOR_COUNT,
	};

	// Global GPU visible descriptor heaps. All other GPU visible descriptor heaps are suballocated from these.
	CbvSrvUavStack cbv_uav_srv_allocator;
	SamplerStack sampler_allocator;

	// Descriptor heaps that are dynamic.
	CbvSrvUavPool cbv_uav_srv_dynamic_allocator;
	SamplerStack gltf_sampler_allocator;

	// Render target and depth stencil views.
	DsvPool dsv_allocator;
	RtvPool rtv_allocator;

	// Per frame descriptor allocators.
	MultiBuffer<CbvSrvUavStack, Config::FRAME_COUNT> cbv_uav_srv_frame_allocators;
	
	void Create(ID3D12Device* device);
	void LoadLookupTables(UploadBuffer* upload_buffer);

	static HRESULT CreateRootSignature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC* desc, ID3D12RootSignature** root_signature, const char* name = nullptr);
	static D3D12_SHADER_BYTECODE LoadShader(const char* filepath);
	static void FreeShader(D3D12_SHADER_BYTECODE shader);
	
	private:

	Microsoft::WRL::ComPtr<ID3D12Device> device;

	// Lookup tables.
	Microsoft::WRL::ComPtr<ID3D12Resource> sheen_e;
};