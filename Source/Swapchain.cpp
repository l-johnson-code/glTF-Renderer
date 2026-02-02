#include "Swapchain.h"

#include <cassert>

#include <dxgi1_5.h>
#include <directx/d3dx12_barriers.h>

void Swapchain::Create(ID3D12Device* device, ID3D12CommandQueue* command_queue, RtvPool* rtv_allocator, HWND window, uint32_t width, uint32_t height) 
{
    HRESULT result = S_OK;

	this->rtv_allocator = rtv_allocator;

	// Create or resize swapchain.
	Microsoft::WRL::ComPtr<IDXGIFactory> factory_0;
	result = CreateDXGIFactory(IID_PPV_ARGS(factory_0.GetAddressOf()));
	assert(result == S_OK);
	result = factory_0.As(&this->dxgi);
	assert(result == S_OK);

	// Query tearing support.
	this->tearing_supported = false;
	Microsoft::WRL::ComPtr<IDXGIFactory5> factory_5;
	result = factory_0.As(&factory_5);
	if (SUCCEEDED(result)) {
		BOOL allow_tearing = false;
		result = factory_5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
		if (SUCCEEDED(result) && allow_tearing) {
			this->tearing_supported = true;
		}
	}

	DXGI_SWAP_CHAIN_DESC1 swap_desc = {
		.Width = width,
		.Height = height,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.Stereo = false,
		.SampleDesc = {
			.Count = 1,
			.Quality = 0,
		},
		.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		.BufferCount = ::Config::FRAME_COUNT,
		.Scaling = DXGI_SCALING_STRETCH,
		.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
		.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED,
		.Flags = this->tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u,
	};
	Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_1;
	result = dxgi->CreateSwapChainForHwnd(command_queue, window, &swap_desc, nullptr, nullptr, &swap_chain_1);
	assert(result == S_OK);

	// Convert to the newer swapchain.
	result = swap_chain_1.As(&this->swap_chain);
	assert(result == S_OK);

	// Stop DXGI from handling Alt+Enter events. 
	// Alt+Enter causes problems with SDL3 because SDL3 doesn't react to WM_SIZE messages so we can't resize the backbuffers in response to the change in fullscreen state, causing a crash.
	// We have to get the swapchains parents factory for this method to work.
	// See https://gamedev.net/forums/topic/634235-dxgidisabling-altenter/4999955/
	Microsoft::WRL::ComPtr<IDXGIFactory> parent;
	result = swap_chain_1->GetParent(IID_PPV_ARGS(&parent));
	if (SUCCEEDED(result)) {
		result = parent->MakeWindowAssociation(window, DXGI_MWA_NO_WINDOW_CHANGES);
	}

	// Get the current backbuffer index from the swapchain.
	current_backbuffer = swap_chain->GetCurrentBackBufferIndex();

	CreateRenderTargetViews(device);
}

void Swapchain::Resize(ID3D12Device* device, uint32_t width, uint32_t height)
{
	HRESULT result = S_OK;

	// Release referenced to back buffers.
	render_targets[0].Reset();
	render_targets[1].Reset();

	// Resize the back buffers.
	DXGI_SWAP_CHAIN_DESC1 desc = {};
	result = swap_chain->GetDesc1(&desc);
	assert(result == S_OK);
	result = swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, desc.Flags);
	assert(result == S_OK);

	// Get the current backbuffer index from the swapchain.
	current_backbuffer = swap_chain->GetCurrentBackBufferIndex();

	CreateRenderTargetViews(device);
}

void Swapchain::CreateRenderTargetViews(ID3D12Device* device)
{
	HRESULT result = S_OK;
	for (int i = 0; i < Config::FRAME_COUNT; i++) {
		result = swap_chain->GetBuffer(i, IID_PPV_ARGS(this->render_targets[i].ReleaseAndGetAddressOf()));
		assert(result == S_OK);
	}

	D3D12_RENDER_TARGET_VIEW_DESC view_desc = {
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
		.Texture2D = {
			.MipSlice = 0,
			.PlaneSlice = 0,
		}
	};
	for (int i = 0; i < Config::FRAME_COUNT; i++) {
		render_target_views[i] = rtv_allocator->AllocateAndCreateRtv(render_targets[i].Get(), &view_desc);
		assert(render_target_views[i].ptr != 0);
	}
}

D3D12_CPU_DESCRIPTOR_HANDLE Swapchain::GetCurrentBackbufferRtv()
{
	int index = GetCurrentBackbufferIndex();
	return render_target_views[index];
}

ID3D12Resource* Swapchain::GetRenderTarget(int backbuffer_index)
{
	return this->render_targets[backbuffer_index].Get();
}

void Swapchain::TransitionBackbufferForRendering(ID3D12GraphicsCommandList* command_list)
{
	// Transition backbuffer to render target.
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(render_targets[current_backbuffer].Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
	command_list->ResourceBarrier(1, &barrier);
}

void Swapchain::TransitionBackbufferForPresenting(ID3D12GraphicsCommandList* command_list)
{
	// Reset render target for swapchain.
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(render_targets[current_backbuffer].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
	command_list->ResourceBarrier(1, &barrier);
}

void Swapchain::Present(ID3D12CommandQueue* command_queue, int sync_interval)
{
    HRESULT result = S_OK;

	// Present the swapchain buffer.
	result = swap_chain->Present(sync_interval, sync_interval == 0 && tearing_supported ? DXGI_PRESENT_ALLOW_TEARING : 0);
	assert(SUCCEEDED(result));

	// Update backbuffer index.
	current_backbuffer = swap_chain->GetCurrentBackBufferIndex();
}

int Swapchain::GetCurrentBackbufferIndex()
{
	return current_backbuffer;
}