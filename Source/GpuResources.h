#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Config.h"
#include "DescriptorAllocator.h"
#include "MultiBuffer.h"
#include "UploadBuffer.h"

class GpuResources {
    public:
    
    enum RenderTargetView {
		RTV_DISPLAY,
		RTV_MOTION_VECTORS,
		RTV_EMISSIVE,
		RTV_SWAPCHAIN_0,
		RTV_SWAPCHAIN_1,
		RTV_COUNT,
	};

	enum DepthStencilView {
		DSV_DEPTH,
		DSV_COUNT,
	};

	enum StaticDescriptor {
		STATIC_DESCRIPTOR_UAV_DISPLAY,
		STATIC_DESCRIPTOR_SRV_DEPTH,
		STATIC_DESCRIPTOR_SRV_MOTION_VECTORS,
		STATIC_DESCRIPTOR_SRV_SHEEN_E,
		STATIC_DESCRIPTOR_COUNT,
	};

	// Global descriptor heaps. All other descriptor heaps are suballocated from these.
	DescriptorStack cbv_uav_srv_allocator;
	DescriptorStack sampler_allocator;

	// Descriptor heaps that are dynamic.
	DescriptorPool cbv_uav_srv_dynamic_allocator;
	DescriptorStack gltf_sampler_allocator;

	// Render targets and depth.
	DescriptorRange dsv_allocator;
	DescriptorRange rtv_allocator;

	// Per frame descriptor allocators.
	MultiBuffer<DescriptorStack, Config::FRAME_COUNT> cbv_uav_srv_frame_allocators;
	
	void Create(ID3D12Device* device);
	D3D12_CPU_DESCRIPTOR_HANDLE GetBackbuffer(int backbuffer_index);
	void LoadLookupTables(UploadBuffer* upload_buffer);

	static D3D12_SHADER_BYTECODE LoadShader(const char* filepath);
	static void FreeShader(D3D12_SHADER_BYTECODE shader);
	
	private:

	Microsoft::WRL::ComPtr<ID3D12Device> device;

	// CPU descriptor heaps.
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> render_target_view_heap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> depth_stencil_view_heap = nullptr;

	// Lookup tables.
	Microsoft::WRL::ComPtr<ID3D12Resource> sheen_e = nullptr;


};