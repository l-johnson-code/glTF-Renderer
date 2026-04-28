#include "GpuResources.h"

#include <cassert>
#include <cstddef>

#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>
#include <directx/dxgiformat.h>
#include <stb/stb_image.h>
#include <tinyexr/tinyexr.h>
#include <glm/gtx/texture.hpp>

#include "Config.h"
#include "DirectXHelpers.h"
#include "File.h"
#include "Profiling.h"

void GpuResources::Create(ID3D12Device* device)
{
	ProfileZoneScoped();
	HRESULT result = S_OK;

	this->device = device;

	// Create the CBV SRV UAV descriptor heap.
	const int total_descriptors = STATIC_DESCRIPTOR_COUNT + Config::DYNAMIC_DESCRIPTORS + Config::FRAME_COUNT * Config::PER_FRAME_DESCRIPTORS;
	cbv_uav_srv_allocator.Create(this->device.Get(), total_descriptors, true);

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
	sampler_allocator.Create(this->device.Get(), Config::MAX_SAMPLERS, true);
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

	// Render target and depth stencil views.
	rtv_allocator.Create(this->device.Get());
	dsv_allocator.Create(this->device.Get());

	allocator.Init(this->device.Get());
}

void GpuResources::LoadLookupTables(UploadBuffer* upload_buffer)
{
	ProfileZoneScoped();
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

		TextureDesc texture_desc = {
			.format = DXGI_FORMAT_R16_FLOAT,
			.width = (uint16_t)x,
			.height = (uint16_t)y,
			.mip_levels = 1,
			.flags = TEXTURE_FLAG_SRV,
			.name = "Sheen E Lookup Table",
		};
		result = CreateTexture(&texture_desc, &this->sheen_e);
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
		device->CreateShaderResourceView(this->sheen_e.Resource(), &srv_desc, descriptor_cpu_handle);

		int stride = x * 2;
		uint32_t row_pitch = 0;
		std::byte* upload_ptr = (std::byte*)upload_buffer->QueueTextureUpload(DXGI_FORMAT_R16_FLOAT, x, y, 1, this->sheen_e.Resource(), 0, &row_pitch);
		for (int i = 0; i < y; i++) {
			memcpy(upload_ptr + row_pitch * i, exr_image.images[0] + stride * i, stride);
		}

		FreeEXRImage(&exr_image);
		FreeEXRHeader(&exr_header);
	}
}

HRESULT GpuResources::CreateBuffer(const BufferDesc* desc, Buffer* buffer)
{
	HRESULT result = S_OK;

	// Create the resource.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(desc->size);
	if (desc->flags & BUFFER_FLAG_UAV) {
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	if (desc->flags & BUFFER_FLAG_RAYTRACING_ACCELERATION_STRUCTURE) {
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |  D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
	};

	result = allocator.CreateCommittedResource(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&resource_desc,
		desc->initial_state,
		nullptr,
		&buffer->resource,
		desc->name ? desc->name : "Buffer"
	);
	if (FAILED(result)) {
		FreeBuffer(buffer);
		return result;
	}

	if (desc->flags & BUFFER_FLAG_PERSISTENT_MAP) {
		result = buffer->resource.resource->Map(0, nullptr, &buffer->pointer);
		if (FAILED(result)) {
			FreeBuffer(buffer);
			return result;
		}
	}

	if (desc->flags & BUFFER_FLAG_GENERATE_DESCRIPTOR) {
		// We assume that the descriptor spans the entire buffer.
		CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc;
		if (desc->structured_byte_stride != 0) {
			srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer(desc->size / desc->structured_byte_stride, desc->structured_byte_stride);
		} else if (desc->format != DXGI_FORMAT_UNKNOWN) {
			srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TypedBuffer(desc->format, (8 * desc->size) / D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(desc->format));
		} else {
			srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::RawBuffer(desc->size / 4);
		}
		buffer->srv = cbv_uav_srv_dynamic_allocator.AllocateAndCreateSrv(buffer->resource.resource.Get(), &srv_desc);
		if (buffer->srv == -1) {
			FreeBuffer(buffer);
			return E_OUTOFMEMORY;
		}
	}

	buffer->size = desc->size;

	return S_OK;
}

void GpuResources::FreeBuffer(Buffer* buffer)
{
	buffer->resource.Reset();
	cbv_uav_srv_dynamic_allocator.Free(buffer->srv);
	buffer->srv = -1;
}

HRESULT GpuResources::CreateTexture(const TextureDesc* desc, Texture* texture)
{
	HRESULT result = S_OK;

	assert(!((desc->flags & TEXTURE_FLAG_RENDER_TARGET) && (desc->flags & TEXTURE_FLAG_DEPTH_TARGET)) && "Textures can not be used both as a render target and depth target");

	// Create the resource.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(
		desc->format, 
		desc->width, 
		desc->height, 
		desc->flags & TEXTURE_FLAG_CUBE ? 6 : 1, 
		desc->mip_levels
	);
	if (!(desc->flags & TEXTURE_FLAG_SRV)) {
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
	}
	if (desc->flags & TEXTURE_FLAG_UAV) {
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	};
	if (desc->flags & TEXTURE_FLAG_RENDER_TARGET) {
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	}
	if (desc->flags & TEXTURE_FLAG_DEPTH_TARGET) {
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	}
	CD3DX12_CLEAR_VALUE* clear_value_ptr = nullptr;
	CD3DX12_CLEAR_VALUE clear_value;
	if (desc->flags & TEXTURE_FLAG_RENDER_TARGET) {
		clear_value = CD3DX12_CLEAR_VALUE(desc->format, desc->clear_color);
		clear_value_ptr = &clear_value;
	}
	if (desc->flags & TEXTURE_FLAG_DEPTH_TARGET) {
		clear_value = CD3DX12_CLEAR_VALUE(desc->format, desc->clear_depth, 0);
		clear_value_ptr = &clear_value;
	}
	result = allocator.CreateCommittedResource(
		&heap_properties,
		D3D12_HEAP_FLAG_NONE,
		&resource_desc,
		desc->initial_state,
		clear_value_ptr,
		&texture->resource,
		desc->name ? desc->name : "Texture"
	);
	if (FAILED(result)) {
		FreeTexture(texture);
		return result;
	}

	// Create a shader resource view.
	DXGI_FORMAT srv_format = desc->format == DXGI_FORMAT_D32_FLOAT ? DXGI_FORMAT_R32_FLOAT : desc->format;
	uint8_t mip_levels = desc->mip_levels == 0 ? glm::levels(glm::u16vec2(desc->width, desc->height)) : desc->mip_levels;
	if (desc->flags & TEXTURE_FLAG_SRV) {
		if (desc->flags & TEXTURE_FLAG_SRV_PER_MIP) {
			texture->srv = cbv_uav_srv_dynamic_allocator.Allocate(mip_levels + 1);
		} else {
			texture->srv = cbv_uav_srv_dynamic_allocator.Allocate(1);
		}
		if (texture->srv == -1) {
			FreeTexture(texture);
			return E_OUTOFMEMORY;
		}
		CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = desc->flags & TEXTURE_FLAG_CUBE ? 
			CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube(srv_format) :
			CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(srv_format);
		cbv_uav_srv_dynamic_allocator.CreateSrv(texture->srv, texture->resource.resource.Get(), &srv_desc);
		if (desc->flags & TEXTURE_FLAG_SRV_PER_MIP) {
			for (int i = 0; i < mip_levels; i++) {
				CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = desc->flags & TEXTURE_FLAG_CUBE ? 
					CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube(srv_format, 1, i) :
					CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(srv_format, 1, i);
				cbv_uav_srv_dynamic_allocator.CreateSrv(texture->srv + 1 + i, texture->resource.resource.Get(), &srv_desc);
			}
		}
	}

	// Create unordered access views.
	DXGI_FORMAT uav_format = srv_format;
	if (desc->flags & TEXTURE_FLAG_UAV) {
		texture->uav = cbv_uav_srv_dynamic_allocator.Allocate(mip_levels);
		if (texture->uav == -1) {
			FreeTexture(texture);
			return E_OUTOFMEMORY;
		}
		for (int i = 0; i < mip_levels; i++) {
			CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc = desc->flags & TEXTURE_FLAG_CUBE ? 
				CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray(uav_format, 6, 0, i) :
				CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D(uav_format, i);
			cbv_uav_srv_dynamic_allocator.CreateUav(texture->uav + i, texture->resource.resource.Get(), nullptr, &uav_desc);
		}
	}

	// Create the render target view.
	if (desc->flags & TEXTURE_FLAG_RENDER_TARGET) { 
		texture->render.rtv = rtv_allocator.CreateRenderTargetView(texture->resource.resource.Get(), nullptr);
		if (texture->render.rtv.ptr == 0) {
			FreeTexture(texture);
			return E_OUTOFMEMORY;
		}
		for (int i = 0; i < 4; i++) {
			texture->render.clear_color[i] = desc->clear_color[i];
		}
	}

	// Create the depth stencil view.
	if (desc->flags & TEXTURE_FLAG_DEPTH_TARGET) { 
		texture->depth.dsv = dsv_allocator.CreateDepthStencilView(texture->resource.resource.Get(), nullptr);
		if (texture->depth.dsv.ptr == 0) {
			FreeTexture(texture);
			return E_OUTOFMEMORY;
		}
		texture->depth.clear_depth = desc->clear_depth;
	}

	texture->width = desc->width;
	texture->height = desc->height;
	texture->mip_levels = mip_levels;
	texture->flags = desc->flags;

	return S_OK;
}

void GpuResources::FreeTexture(Texture* texture)
{
	texture->resource.Reset();
	cbv_uav_srv_dynamic_allocator.Free(texture->srv);
	texture->srv = -1;
	cbv_uav_srv_dynamic_allocator.Free(texture->uav);
	texture->uav = -1;
	if (texture->flags & TEXTURE_FLAG_RENDER_TARGET) {
		rtv_allocator.FreeDescriptor(texture->render.rtv);
		texture->render.rtv = {0};
	}
	if (texture->flags & TEXTURE_FLAG_DEPTH_TARGET) {
		dsv_allocator.FreeDescriptor(texture->depth.dsv);
		texture->depth.dsv = {0};
	}
}

HRESULT GpuResources::CreateRootSignature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC* desc, ID3D12RootSignature** root_signature, const char* name)
{
	ProfileZoneScoped();
	HRESULT result = S_OK;
	Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob;
	result = D3D12SerializeRootSignature(desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &root_signature_blob, nullptr);
	if (FAILED(result)) {
		return result;
	}
	result = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(root_signature));
	if (FAILED(result)) {
		return result;
	}
	if (name) {
		SetName(*root_signature, name);
	}
	return result;
}

HRESULT GpuResources::CreateComputePipelineState(ID3D12Device* device, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** pipeline_state, const char* name)
{
	ProfileZoneScoped();
	HRESULT result = device->CreateComputePipelineState(desc, IID_PPV_ARGS(pipeline_state));
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return result;
	}
	if (name) {
		SetName(*pipeline_state, name);
	}
	return result;
}

HRESULT GpuResources::CreateGraphicsPipelineState(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** pipeline_state, const char* name)
{
	ProfileZoneScoped();
	HRESULT result = device->CreateGraphicsPipelineState(desc, IID_PPV_ARGS(pipeline_state));
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return result;
	}
	if (name) {
		SetName(*pipeline_state, name);
	}
	return result;
}

D3D12_SHADER_BYTECODE GpuResources::LoadShader(const char* filepath)
{
	ProfileZoneScoped();
    D3D12_SHADER_BYTECODE shader_bytecode = {};
	shader_bytecode.pShaderBytecode = File::Load(filepath, &shader_bytecode.BytecodeLength);
    return shader_bytecode;
}

void GpuResources::FreeShader(D3D12_SHADER_BYTECODE bytecode)
{
	ProfileZoneScoped();
	if (bytecode.pShaderBytecode) {
    	File::Free((void*)bytecode.pShaderBytecode);
	}
}