#pragma once

#include <directx/d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "Config.h"
#include "GpuResources.h"

class Swapchain {

	public:

    void Create(ID3D12Device* device, ID3D12CommandQueue* command_queue, DescriptorPool* rtv_allocator, HWND window, uint32_t width, uint32_t height);
	void Resize(ID3D12Device* device, uint32_t width, uint32_t height);
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackbufferRtv();
	void TransitionBackbufferForRendering(ID3D12GraphicsCommandList* command_list);
	void TransitionBackbufferForPresenting(ID3D12GraphicsCommandList* command_list);
	void Present(ID3D12CommandQueue* command_queue, int sync_interval);
	ID3D12Resource* GetRenderTarget(int backbuffer_index);
	
	private:
	
	Microsoft::WRL::ComPtr<IDXGIFactory4> dxgi = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> render_targets[Config::FRAME_COUNT] = {};
	DescriptorPool* rtv_allocator = nullptr;
	
	int render_target_views[Config::FRAME_COUNT] = {-1, -1};
	uint32_t current_backbuffer = 0;
	bool tearing_supported = false;
	
	void CreateRenderTargetViews(ID3D12Device* device);
	int GetCurrentBackbufferIndex();
};
