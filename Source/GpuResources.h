#pragma once

#include <directx/d3d12.h>
#include <wrl/client.h>

#include "Config.h"
#include "DescriptorAllocator.h"
#include "EnumFlag.h"
#include "MultiBuffer.h"
#include "GpuAllocator.h"

namespace Gpu {

enum BufferFlags : uint8_t {
	BUFFER_FLAG_NONE = 0,
	BUFFER_FLAG_UAV = 1 << 0,
	BUFFER_FLAG_RAYTRACING_ACCELERATION_STRUCTURE = 1 << 1,
	BUFFER_FLAG_PERSISTENT_MAP = 1 << 2,
	BUFFER_FLAG_GENERATE_DESCRIPTOR = 1 << 3,
};
DEFINE_ENUM_FLAG(BufferFlags);

struct BufferDesc {
	uint64_t size = 0;
	BufferFlags flags = BUFFER_FLAG_NONE;
	D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	uint32_t structured_byte_stride = 0;
	D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT;
	const char* name = nullptr;
};

class Buffer {
	public:
	ID3D12Resource* Resource() { return this->resource; }
	uint64_t Size() { return this->size; }
	int Srv() { assert(this->srv != -1); return this->srv; };
	int Uav() { assert(this->uav != -1); return this->uav; };
	void* Pointer() { assert(pointer); return this->pointer; }
	
	private:
	friend class Resources;

	ID3D12Resource* resource = nullptr;
	GpuAllocation allocation;
	uint64_t size = 0;
	int srv = -1;
	int uav = -1;
	void* pointer = nullptr;
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
DEFINE_ENUM_FLAG(TextureFlags);

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
	const char* name = nullptr;
};

class Texture {
	public:
	ID3D12Resource* Resource() { return this->resource; }
	uint16_t Width() { return this->width; }
	uint16_t Height() { return this->height; }
	uint8_t MipLevels() { return this->mip_levels; };
	int Srv() { assert(this->srv != -1); return this->srv; };
	int Srv(int mip) { assert((this->flags & TEXTURE_FLAG_SRV_PER_MIP) && (this->srv != -1) && (mip < this->mip_levels)); return this->srv + 1 + mip; };
	int Uav() { assert(this->uav != -1); return this->uav; }
	int Uav(int mip) { assert((this->uav != -1) && (mip < this->mip_levels)); return this->uav + mip; };
	D3D12_CPU_DESCRIPTOR_HANDLE Rtv() { assert((this->flags & TEXTURE_FLAG_RENDER_TARGET) && (this->render.rtv.ptr != 0)); return this->render.rtv; }
	const float* ClearColor() { assert(this->flags & TEXTURE_FLAG_RENDER_TARGET); return &this->render.clear_color[0]; }
	D3D12_CPU_DESCRIPTOR_HANDLE Dsv() { assert((this->flags & TEXTURE_FLAG_DEPTH_TARGET) && (this->depth.dsv.ptr != 0)); return this->depth.dsv; }
	float ClearDepth() { assert(this->flags & TEXTURE_FLAG_DEPTH_TARGET); return this->depth.clear_depth; }
	// TODO: This is a for a temporary fix and should be removed. 
	void Invalidate() 
	{ 
		resource = nullptr;
		srv = -1;
		uav = -1;
	}

	private:
	friend class Resources;

	ID3D12Resource* resource = nullptr;
	GpuAllocation allocation;
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

class Resources {
    public:

	enum StaticDescriptor {
		STATIC_DESCRIPTOR_SRV_SHEEN_E,
		STATIC_DESCRIPTOR_COUNT,
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
	HRESULT CreateBuffer(const BufferDesc* desc, Buffer* buffer);
	void FreeBuffer(Buffer* buffer);
	HRESULT CreateTexture(const TextureDesc* desc, Texture* texture);
	void FreeTexture(Texture* texture);
	HRESULT CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc, ID3D12RootSignature** root_signature, const char* name = nullptr);
	HRESULT CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** pipeline_state, const char* name = nullptr);
	HRESULT CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** pipeline_state, const char* name = nullptr);
	HRESULT CreateStateObject(const D3D12_STATE_OBJECT_DESC* desc, ID3D12StateObject** state_object, const char* name = nullptr);
	int CreateShaderResourceView(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc);
	void FreeResourceDescriptor(int descriptor);
	static D3D12_SHADER_BYTECODE LoadShader(const char* filepath);
	static void FreeShader(D3D12_SHADER_BYTECODE shader);
	
	private:

	Microsoft::WRL::ComPtr<ID3D12Device> device;
};

}