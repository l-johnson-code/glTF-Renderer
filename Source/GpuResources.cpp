#include "GpuResources.h"

#include <cassert>
#include <cstddef>

#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>
#include <directx/dxgiformat.h>
#include <stb/stb_image.h>
#include <tinyexr/tinyexr.h>

#include "Config.h"
#include "File.h"

void GpuResources::Create(ID3D12Device* device)
{
	HRESULT result = S_OK;

	this->device = device;

	// Create the CBV SRV UAV descriptor heap.
	const int total_descriptors = STATIC_DESCRIPTOR_COUNT + Config::DYNAMIC_DESCRIPTORS + Config::FRAME_COUNT * Config::PER_FRAME_DESCRIPTORS;
	D3D12_DESCRIPTOR_HEAP_DESC desc_heap = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		.NumDescriptors = total_descriptors,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
	};
	cbv_uav_srv_allocator.Create(this->device.Get(), &desc_heap);

	// Allocate static descriptors.
	cbv_uav_srv_allocator.Allocate(STATIC_DESCRIPTOR_COUNT);

	// Allocate dynamic descriptors.
	int dynamic_descriptor_start = cbv_uav_srv_allocator.Allocate(Config::DYNAMIC_DESCRIPTORS);
	cbv_uav_srv_dynamic_allocator.Create(&cbv_uav_srv_allocator, dynamic_descriptor_start, Config::DYNAMIC_DESCRIPTORS);

	// Allocate per frame descriptors.
	for (int i = 0; i < cbv_uav_srv_frame_allocators.Size(); i++) {
		int descriptor_start = cbv_uav_srv_allocator.Allocate(Config::PER_FRAME_DESCRIPTORS);
		cbv_uav_srv_frame_allocators[i].Create(&cbv_uav_srv_allocator, descriptor_start, Config::PER_FRAME_DESCRIPTORS);
	}

	// Create the sampler descriptor heap.
	desc_heap = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
		.NumDescriptors = Config::MAX_SAMPLERS,
		.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
	};
	sampler_allocator.Create(this->device.Get(), &desc_heap);
	// Create a default sampler at index 0 so that 0 can be used as a valid default value when indexing the sampler heap.
	int default_sampler_index = sampler_allocator.Allocate(1);
	assert(default_sampler_index == 0);
	D3D12_SAMPLER_DESC default_sampler = {
		.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		.MipLODBias = 0.0f,
		.MaxAnisotropy = 1,
		.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE,
		.BorderColor = {0.0f, 0.0f, 0.0f, 0.0f},
		.MinLOD = 0.0f,
		.MaxLOD = D3D12_FLOAT32_MAX,
	};
	this->device->CreateSampler(&default_sampler, sampler_allocator.GetCpuHandle(default_sampler_index));

	int gltf_sampler_count = sampler_allocator.Capacity() - sampler_allocator.Size();
	int gltf_samplers_start = sampler_allocator.Allocate(gltf_sampler_count);
	gltf_sampler_allocator.Create(&sampler_allocator, gltf_samplers_start, gltf_sampler_count); // Dynamic samplers.

	// Render target view descriptors.
	desc_heap = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		.NumDescriptors = Config::MAX_RENDER_TARGET_VIEWS,
	};
	rtv_allocator.Create(this->device.Get(), &desc_heap);

	// Depth stencil descriptors.
	desc_heap = {
		.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		.NumDescriptors = Config::MAX_DEPTH_STENCIL_VIEWS
	};
	dsv_allocator.Create(this->device.Get(), &desc_heap);
}

void GpuResources::LoadLookupTables(UploadBuffer* upload_buffer)
{
	HRESULT result = S_OK;
	{
		int x, y;
		const char* file = "Sheen_E.exr";
		const char* error;

		EXRVersion exr_version;
		int ret = ParseEXRVersionFromFile(&exr_version, file);
		assert(ret == TINYEXR_SUCCESS);
		assert(exr_version.tiled == 0);
		assert(exr_version.multipart == 0);
		assert(exr_version.non_image == 0);

		EXRHeader exr_header;
		InitEXRHeader(&exr_header);
		ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, file, &error);
		assert(ret == TINYEXR_SUCCESS);
		assert(exr_header.num_channels == 1);
		assert(exr_header.channels[0].pixel_type == TINYEXR_PIXELTYPE_HALF);

		EXRImage exr_image;
		InitEXRImage(&exr_image);

		ret = LoadEXRImageFromFile(&exr_image, &exr_header, file, &error);
		assert(ret == TINYEXR_SUCCESS);
		x = exr_image.width;
		y = exr_image.height;

		CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
		CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_FLOAT, x, y, 1, 1);
		
		result = this->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(this->sheen_e.ReleaseAndGetAddressOf()));
		assert(result == S_OK);
		result = sheen_e->SetName(L"Sheen E Lookup Table");
		assert(result == S_OK);

		D3D12_CPU_DESCRIPTOR_HANDLE descriptor_cpu_handle = cbv_uav_srv_allocator.GetCpuHandle(GpuResources::STATIC_DESCRIPTOR_SRV_SHEEN_E);

		D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			.Format = DXGI_FORMAT_R16_FLOAT,
			.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
			.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = std::numeric_limits<uint32_t>::max()
			}
		};
		device->CreateShaderResourceView(this->sheen_e.Get(), &srv_desc, descriptor_cpu_handle);

		int stride = x * 2;
		uint32_t row_pitch = 0;
		std::byte* upload_ptr = (std::byte*)upload_buffer->QueueTextureUpload(DXGI_FORMAT_R16_FLOAT, x, y, 1, this->sheen_e.Get(), 0, &row_pitch);
		for (int i = 0; i < y; i++) {
			memcpy(upload_ptr + row_pitch * i, exr_image.images[0] + stride * i, stride);
		}

		FreeEXRImage(&exr_image);
		FreeEXRHeader(&exr_header);
	}
}

D3D12_SHADER_BYTECODE GpuResources::LoadShader(const char* filepath)
{
    D3D12_SHADER_BYTECODE shader_bytecode = {};
	shader_bytecode.pShaderBytecode = File::Load(filepath, &shader_bytecode.BytecodeLength);
    return shader_bytecode;
}

void GpuResources::FreeShader(D3D12_SHADER_BYTECODE bytecode)
{
	if (bytecode.pShaderBytecode) {
    	File::Free((void**)&bytecode.pShaderBytecode);
	}
}