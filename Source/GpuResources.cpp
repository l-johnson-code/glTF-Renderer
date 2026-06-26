#include "GpuResources.h"

#include <cassert>

#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>
#include <directx/d3dx12_root_signature.h>
#include <directx/dxgiformat.h>
#include <glm/gtx/texture.hpp>
#include <stb/stb_image.h>

#include "Config.h"
#include "DirectXHelpers.h"
#include "File.h"
#include "Profiling.h"

namespace Gpu {

void Resources::Create(ID3D12Device* device)
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

	CD3DX12_STATIC_SAMPLER_DESC static_samplers[3];
	static_samplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
	static_samplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
	static_samplers[2].Init(2, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	CD3DX12_ROOT_PARAMETER compute_root_parameters[GENERIC_COMPUTE_ROOT_PARAMETER_COUNT];
	compute_root_parameters[GENERIC_COMPUTE_ROOT_PARAMETER_CONSTANT_BUFFER].InitAsConstantBufferView(0);

	CD3DX12_ROOT_SIGNATURE_DESC compute_root_signature_desc(
		std::size(compute_root_parameters), 
		compute_root_parameters, 
		std::size(static_samplers), 
		static_samplers, 
		D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | 
		D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED
	);
	result = CreateRootSignature(&compute_root_signature_desc, &this->generic_compute_root_signature, "Generic Compute Root Signature");
	assert(SUCCEEDED(result));

	CD3DX12_ROOT_PARAMETER graphics_root_parameters[GENERIC_GRAPHICS_ROOT_PARAMETER_COUNT];
	graphics_root_parameters[GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_FRAME].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	graphics_root_parameters[GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_VERTEX_PER_DRAW].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	graphics_root_parameters[GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_FRAME].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_PIXEL);
	graphics_root_parameters[GENERIC_GRAPHICS_ROOT_PARAMETER_CONSTANT_BUFFER_PIXEL_PER_DRAW].InitAsConstantBufferView(1, 0 , D3D12_SHADER_VISIBILITY_PIXEL);
	
	CD3DX12_ROOT_SIGNATURE_DESC graphics_root_signature_desc(
		std::size(graphics_root_parameters), 
		graphics_root_parameters, 
		std::size(static_samplers), 
		static_samplers, 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
		D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | 
		D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED
	);
	result = CreateRootSignature(&graphics_root_signature_desc, &this->generic_graphics_root_signature, "Generic Graphics Root Signature");
	assert(SUCCEEDED(result));
}

HRESULT Resources::CreateBuffer(const BufferDesc* desc, Buffer* buffer)
{
	HRESULT result = S_OK;

	// Create the resource.
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(desc->size);
	if (desc->flags & BUFFER_FLAG_UAV) {
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	}
	if (desc->flags & BUFFER_FLAG_RAYTRACING_ACCELERATION_STRUCTURE) {
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |  D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
	};
	result = allocator.CreateResource(
		desc->heap_type,
		&resource_desc,
		desc->initial_state,
		nullptr,
		desc->name ? desc->name : "Buffer",
		&buffer->allocation,
		IID_PPV_ARGS(&buffer->resource)
	);
	if (FAILED(result)) {
		FreeBuffer(buffer);
		return result;
	}

	// Map the resource.
	if (desc->flags & BUFFER_FLAG_PERSISTENT_MAP) {
		result = buffer->resource->Map(0, nullptr, &buffer->pointer);
		if (FAILED(result)) {
			FreeBuffer(buffer);
			return result;
		}
	}

	// Create descriptor. We assume that the descriptor spans the entire buffer.
	if (desc->flags & BUFFER_FLAG_GENERATE_DESCRIPTOR) {
		CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc;
		if (desc->flags & BUFFER_FLAG_RAYTRACING_ACCELERATION_STRUCTURE) {
			srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::RaytracingAccelStruct(buffer->resource->GetGPUVirtualAddress());
		} else if (desc->structured_byte_stride != 0) {
			srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer(desc->size / desc->structured_byte_stride, desc->structured_byte_stride);
		} else if (desc->format != DXGI_FORMAT_UNKNOWN) {
			srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TypedBuffer(desc->format, (8 * desc->size) / D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(desc->format));
		} else {
			srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::RawBuffer(desc->size / 4);
			srv_desc.Format = DXGI_FORMAT_R32_TYPELESS; // The RawBuffer function sets the format incorrectly, so we reset it here.
		}
		buffer->srv = cbv_uav_srv_dynamic_allocator.AllocateAndCreateSrv(desc->flags & BUFFER_FLAG_RAYTRACING_ACCELERATION_STRUCTURE ? nullptr : buffer->resource, &srv_desc);
		if (buffer->srv == -1) {
			FreeBuffer(buffer);
			return E_OUTOFMEMORY;
		}
	}

	// Create UAV descriptor.
	if ((desc->flags & BUFFER_FLAG_GENERATE_DESCRIPTOR) && (desc->flags & BUFFER_FLAG_UAV)) {
		CD3DX12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
		if (desc->structured_byte_stride != 0) {
			uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::StructuredBuffer(desc->size / desc->structured_byte_stride, desc->structured_byte_stride);
		} else if (desc->format != DXGI_FORMAT_UNKNOWN) {
			uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::TypedBuffer(desc->format, (8 * desc->size) / D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(desc->format));
		} else {
			uav_desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::RawBuffer(desc->size / 4);
		}
		buffer->uav = cbv_uav_srv_dynamic_allocator.AllocateAndCreateUav(buffer->resource, nullptr, &uav_desc);
		if (buffer->uav == -1) {
			FreeBuffer(buffer);
			return E_OUTOFMEMORY;
		}
	}

	buffer->size = desc->size;

	return S_OK;
}

void Resources::FreeBuffer(Buffer* buffer)
{
	if (buffer->resource) {
		buffer->resource->Release();
		buffer->resource = nullptr;
	}
	buffer->allocation.Free();
	buffer->resource = nullptr;
	cbv_uav_srv_dynamic_allocator.Free(buffer->srv);
	buffer->srv = -1;
	cbv_uav_srv_dynamic_allocator.Free(buffer->uav);
	buffer->uav = -1;
}

HRESULT Resources::CreateTexture(const TextureDesc* desc, Texture* texture)
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
	result = allocator.CreateResource(
		D3D12_HEAP_TYPE_DEFAULT,
		&resource_desc,
		desc->initial_state,
		clear_value_ptr,
		desc->name ? desc->name : "Texture",
		&texture->allocation,
		IID_PPV_ARGS(&texture->resource)
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
		cbv_uav_srv_dynamic_allocator.CreateSrv(texture->srv, texture->resource, &srv_desc);
		if (desc->flags & TEXTURE_FLAG_SRV_PER_MIP) {
			for (int i = 0; i < mip_levels; i++) {
				CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = desc->flags & TEXTURE_FLAG_CUBE ? 
					CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube(srv_format, 1, i) :
					CD3DX12_SHADER_RESOURCE_VIEW_DESC::Tex2D(srv_format, 1, i);
				cbv_uav_srv_dynamic_allocator.CreateSrv(texture->srv + 1 + i, texture->resource, &srv_desc);
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
			cbv_uav_srv_dynamic_allocator.CreateUav(texture->uav + i, texture->resource, nullptr, &uav_desc);
		}
	}

	// Create the render target view.
	if (desc->flags & TEXTURE_FLAG_RENDER_TARGET) { 
		texture->render.rtv = rtv_allocator.CreateRenderTargetView(texture->resource, nullptr);
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
		texture->depth.dsv = dsv_allocator.CreateDepthStencilView(texture->resource, nullptr);
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

void Resources::FreeTexture(Texture* texture)
{
	if (texture->resource) {
		texture->resource->Release();
		texture->resource = nullptr;
	}
	texture->allocation.Free();
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

HRESULT Resources::CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC* desc, ID3D12RootSignature** root_signature, const char* name)
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

HRESULT Resources::CreateComputePipelineState(const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** pipeline_state, const char* name)
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

HRESULT Resources::CreateComputePipelineState(const ComputePipelineDesc* desc, ID3D12PipelineState** pipeline_state)
{
	HRESULT result = S_OK;
	D3D12_COMPUTE_PIPELINE_STATE_DESC d3d12_desc = {};

	// Load shaders.
	std::string shader_path("Shaders/");
	shader_path.append(desc->compute_shader);
	shader_path.append(".cs.bin");
	d3d12_desc.CS = LoadShader(shader_path.c_str());
	if (d3d12_desc.CS.pShaderBytecode == nullptr) {
		FreeShader(d3d12_desc.CS);
		return E_FAIL;
	}

	// TODO: Check if pipeline exists in cache, create pipeline from cache if it does.

	// Create pipeline from scratch.
	d3d12_desc.pRootSignature = generic_compute_root_signature.Get();
	result = this->device->CreateComputePipelineState(&d3d12_desc, IID_PPV_ARGS(pipeline_state));
	if (FAILED(result)) {
		FreeShader(d3d12_desc.CS);
		return result;
	}
	SetName(*pipeline_state, desc->name);

	// TODO: Store pipeline in cache.
	
	return S_OK;
}

HRESULT Resources::CreateGraphicsPipelineState(const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, ID3D12PipelineState** pipeline_state, const char* name)
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

HRESULT Resources::CreateGraphicsPipelineState(const GraphicsPipelineDesc* desc, ID3D12PipelineState** pipeline_state)
{
	HRESULT result = S_OK;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC d3d12_desc = {};

	// Load shaders.
	std::string shader_path("Shaders/");
	shader_path.append(desc->vertex_shader);
	shader_path.append(".vs.bin");
	d3d12_desc.VS = LoadShader(shader_path.c_str());
	if (d3d12_desc.VS.pShaderBytecode == nullptr) {
		FreeShader(d3d12_desc.VS);
		return E_FAIL;
	}

	shader_path = std::string("Shaders/");
	shader_path.append(desc->pixel_shader);
	shader_path.append(".ps.bin");
	d3d12_desc.PS = LoadShader(shader_path.c_str());
	if (d3d12_desc.PS.pShaderBytecode == nullptr) {
		FreeShader(d3d12_desc.VS);
		FreeShader(d3d12_desc.PS);
		return E_FAIL;
	}

	// TODO: Check if pipeline exists in cache, create pipeline from cache if it does.

	// Create pipeline from scratch.
	d3d12_desc.pRootSignature = generic_graphics_root_signature.Get();
	d3d12_desc.BlendState = desc->blend_state;
	d3d12_desc.SampleMask = std::numeric_limits<UINT>::max();
	d3d12_desc.InputLayout = desc->input_layout;
	d3d12_desc.RasterizerState = desc->rasterizer_state;
	d3d12_desc.DepthStencilState = desc->depth_stencil_state;
	d3d12_desc.PrimitiveTopologyType = desc->primitive_topology_type;
	d3d12_desc.NumRenderTargets = desc->render_target_count;
	for (int i = 0; i < std::size(desc->render_target_formats); i++) {
		d3d12_desc.RTVFormats[i] = desc->render_target_formats[i];
	}
	d3d12_desc.DSVFormat = desc->depth_stencil_format;
	d3d12_desc.SampleDesc.Count = 1;
	d3d12_desc.SampleDesc.Quality = 0;
	result = this->device->CreateGraphicsPipelineState(&d3d12_desc, IID_PPV_ARGS(pipeline_state));
	if (FAILED(result)) {
		FreeShader(d3d12_desc.VS);
		FreeShader(d3d12_desc.PS);
		return result;
	}
	SetName(*pipeline_state, desc->name);

	// TODO: Store pipeline in cache.
	
	return S_OK;
}

HRESULT Resources::CreateStateObject(const D3D12_STATE_OBJECT_DESC* desc, ID3D12StateObject** state_object, const char* name)
{
	ProfileZoneScoped();
	Microsoft::WRL::ComPtr<ID3D12Device5> device_5;
	HRESULT result = device.As(&device_5);
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return result;
	}
	result = device_5->CreateStateObject(desc, IID_PPV_ARGS(state_object));
	assert(SUCCEEDED(result));
	if (FAILED(result)) {
		return result;
	}
	if (name) {
		SetName(*state_object, name);
	}
	return result;
}

int Resources::CreateShaderResourceView(ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc)
{
	return this->cbv_uav_srv_dynamic_allocator.AllocateAndCreateSrv(resource, desc);
}

int Resources::CreateUnorderedAccessView(ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc)
{
	return this->cbv_uav_srv_dynamic_allocator.AllocateAndCreateUav(resource, nullptr, desc);
}

void Resources::FreeResourceDescriptor(int descriptor)
{
	this->cbv_uav_srv_dynamic_allocator.Free(descriptor);
}

D3D12_SHADER_BYTECODE Resources::LoadShader(const char* filepath)
{
	ProfileZoneScoped();
    D3D12_SHADER_BYTECODE shader_bytecode = {};
	shader_bytecode.pShaderBytecode = File::Load(filepath, &shader_bytecode.BytecodeLength);
    return shader_bytecode;
}

void Resources::FreeShader(D3D12_SHADER_BYTECODE bytecode)
{
	ProfileZoneScoped();
	if (bytecode.pShaderBytecode) {
    	File::Free((void*)bytecode.pShaderBytecode);
	}
}

}