#include "Mesh.h"

#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>

void VertexBuffer::Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t vertex_count, DXGI_FORMAT format, const wchar_t* name)
{
	UINT vertex_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	
	// Create the resource for all our vertex data.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(vertex_count * vertex_size);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(result));
	resource->SetName(name);
	assert(SUCCEEDED(result));
	
	// Create a vertex buffer view.
	view = {
		.BufferLocation = this->resource->GetGPUVirtualAddress(),
		.SizeInBytes = vertex_count * vertex_size,
		.StrideInBytes = vertex_size,
	};

	// Create a descriptor.
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TypedBuffer(format, vertex_count);
	this->descriptor = descriptor_allocator->AllocateAndCreateSrv(resource.Get(), &srv_desc);
}

void VertexBuffer::Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t vertex_count, uint32_t vertex_size, const wchar_t* name)
{	
	// Create the resource for all our vertex data.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(vertex_count * vertex_size);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(result));
	resource->SetName(name);
	assert(SUCCEEDED(result));
	
	// Create a vertex buffer view.
	view = {
		.BufferLocation = resource->GetGPUVirtualAddress(),
		.SizeInBytes = vertex_count * vertex_size,
		.StrideInBytes = vertex_size,
	};

	// Create a descriptor.
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer(vertex_count, vertex_size);
	this->descriptor = descriptor_allocator->AllocateAndCreateSrv(resource.Get(), &srv_desc);
}

void VertexBuffer::Destroy(DescriptorPool* descriptor_allocator)
{
	view = {};
	descriptor_allocator->Free(descriptor);
	descriptor = -1;
	resource.Reset();
}

void* VertexBuffer::QueueUpdate(UploadBuffer* upload_buffer)
{
	return upload_buffer->QueueBufferUpload(resource->GetDesc().Width, resource.Get(), 0);
}

void IndexBuffer::Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t index_count, DXGI_FORMAT format, const wchar_t* name)
{
	UINT index_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	
	// Create the resource for all our vertex data.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(index_count * index_size);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(result));
	resource->SetName(name);
	assert(SUCCEEDED(result));
	
	// Create a index buffer view.
	view = {
		.BufferLocation = resource->GetGPUVirtualAddress(),
		.SizeInBytes = index_count * index_size,
		.Format = format,
	};

	// Create a descriptor.
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TypedBuffer(format, index_count);
	this->descriptor = descriptor_allocator->AllocateAndCreateSrv(resource.Get(), &srv_desc);
}

void IndexBuffer::Destroy(DescriptorPool* descriptor_allocator)
{
	view = {};
	descriptor_allocator->Free(descriptor);
	descriptor = -1;
	resource.Reset();
}

void* IndexBuffer::QueueUpdate(UploadBuffer* upload_buffer)
{
	return upload_buffer->QueueBufferUpload(resource->GetDesc().Width, resource.Get(), 0);
}

void DynamicVertexBuffer::Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t vertex_count, DXGI_FORMAT format, const wchar_t* name)
{
	UINT vertex_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	
	// Create the resource for all our vertex data.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(vertex_count * vertex_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(result));
	resource->SetName(name);
	assert(SUCCEEDED(result));
	
	// Create a vertex buffer view.
	view = {
		.BufferLocation = this->resource->GetGPUVirtualAddress(),
		.SizeInBytes = vertex_count * vertex_size,
		.StrideInBytes = vertex_size,
	};

	// Create a descriptor.
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TypedBuffer(format, vertex_count);
	this->descriptor = descriptor_allocator->AllocateAndCreateSrv(resource.Get(), &srv_desc);
}

void DynamicVertexBuffer::Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t vertex_count, uint32_t vertex_size, const wchar_t* name)
{	
	// Create the resource for all our vertex data.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(vertex_count * vertex_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(result));
	resource->SetName(name);
	assert(SUCCEEDED(result));
	
	// Create a vertex buffer view.
	view = {
		.BufferLocation = resource->GetGPUVirtualAddress(),
		.SizeInBytes = vertex_count * vertex_size,
		.StrideInBytes = vertex_size,
	};

	// Create a descriptor.
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer(vertex_count, vertex_size);
	this->descriptor = descriptor_allocator->AllocateAndCreateSrv(resource.Get(), &srv_desc);
}

void DynamicVertexBuffer::Destroy(DescriptorPool* descriptor_allocator)
{
	view = {};
	descriptor_allocator->Free(descriptor);
	descriptor = -1;
	resource.Reset();
}

void Mesh::Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, const Desc* desc)
{
	this->topology = desc->topology;
	this->flags = desc->flags;
	this->num_of_indices = desc->num_of_indices;
	this->num_of_vertices = desc->num_of_vertices;

	if (desc->flags & FLAG_INDEX) {
    	index.Create(device, descriptor_allocator, num_of_indices, desc->index_format, L"Index");
	}
	position.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT, L"Position");
    if (desc->flags & FLAG_NORMAL) {
		normal.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT, L"Normal");
	}
    if (desc->flags & FLAG_TANGENT) {
		tangent.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT, L"Tangent");
	}
    if (desc->flags & FLAG_TEXCOORD_0) {
		texcoords[0].Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32_FLOAT, L"Texcoord 0");
	}
    if (desc->flags & FLAG_TEXCOORD_1) {
		texcoords[1].Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32_FLOAT, L"Texcoord 1");
	}
    if (desc->flags & FLAG_COLOR) {
		color.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT, L"Color");
	}
    if (desc->flags & FLAG_JOINT_WEIGHT) {
		joint_weight.Create(device, descriptor_allocator, num_of_vertices, sizeof(JointWeight), L"Joint Weight");
	}
}

void Mesh::Destroy(DescriptorPool* descriptor_allocator)
{
	index.Destroy(descriptor_allocator);
	position.Destroy(descriptor_allocator);
	normal.Destroy(descriptor_allocator);
	tangent.Destroy(descriptor_allocator);
	texcoords[0].Destroy(descriptor_allocator);
	texcoords[1].Destroy(descriptor_allocator);
	color.Destroy(descriptor_allocator);
	joint_weight.Destroy(descriptor_allocator);
}

void DynamicMesh::Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, const Desc* desc)
{
	this->flags = desc->flags;
	this->num_of_vertices = desc->num_of_vertices;
	this->current_position_buffer = 0;

	if (desc->flags & FLAG_POSITION) {
		position[0].Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT, L"Position");
		position[1].Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT, L"Position");
	}
    if (desc->flags & FLAG_NORMAL) {
		normal.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT, L"Normal");
	}
    if (desc->flags & FLAG_TANGENT) {
		tangent.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT, L"Tangent");
	}
}

void DynamicMesh::Destroy(DescriptorPool* descriptor_allocator)
{
	position[0].Destroy(descriptor_allocator);
	position[1].Destroy(descriptor_allocator);
	normal.Destroy(descriptor_allocator);
	tangent.Destroy(descriptor_allocator);
}

void DynamicMesh::Flip()
{
	this->current_position_buffer = (this->current_position_buffer + 1) % 1;
}

DynamicVertexBuffer* DynamicMesh::GetCurrentPositionBuffer()
{
	return &position[current_position_buffer];
}

DynamicVertexBuffer* DynamicMesh::GetPreviousPositionBuffer()
{
	return &position[(current_position_buffer - 1) % 1];
}

void MorphTarget::Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, const Desc* desc)
{
	this->flags = desc->flags;
	this->num_of_vertices = desc->num_of_vertices;

	if (desc->flags & FLAG_POSITION) {
		position.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT, L"Position");
	}
    if (desc->flags & FLAG_NORMAL) {
		normal.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT, L"Normal");
	}
    if (desc->flags & FLAG_TANGENT) {
		tangent.Create(device, descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT, L"Tangent");
	}
}

void MorphTarget::Destroy(DescriptorPool* descriptor_allocator)
{
	position.Destroy(descriptor_allocator);
	normal.Destroy(descriptor_allocator);
	tangent.Destroy(descriptor_allocator);
}