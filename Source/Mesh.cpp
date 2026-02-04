#include "Mesh.h"

#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>

#include "Memory.h"

static uint64_t CalculateTotalAllocationSize(int allocation_count, const VertexAllocation* allocations, uint64_t* offsets)
{
	uint64_t size = 0;
	for (int i = 0; i < allocation_count; i++) {
		uint64_t offset = Align(size, allocations[i].alignment);
		size = offset + allocations[i].size;
		offsets[i] = offset;
	}
	return size;
}

VertexAllocation VertexBuffer::GetAllocationSize(uint32_t vertex_count, DXGI_FORMAT format)
{
	uint32_t vertex_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	return {vertex_count * vertex_size, vertex_size};
}

VertexAllocation VertexBuffer::GetAllocationSize(uint32_t vertex_count, uint32_t element_size)
{
	return {vertex_count * element_size, element_size};
}

void VertexBuffer::Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, CbvSrvUavPool* descriptor_allocator, uint32_t vertex_count, DXGI_FORMAT format)
{
	UINT vertex_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
		
	// Create a vertex buffer view.
	view = {
		.BufferLocation = buffer,
		.SizeInBytes = vertex_count * vertex_size,
		.StrideInBytes = vertex_size,
	};

	// Create a descriptor.
	uint64_t first_element = (buffer - resource->GetGPUVirtualAddress()) / vertex_size;
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TypedBuffer(format, vertex_count, first_element);
	this->descriptor = descriptor_allocator->AllocateAndCreateSrv(resource, &srv_desc);
}

void VertexBuffer::Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, CbvSrvUavPool* descriptor_allocator, uint32_t vertex_count, uint32_t vertex_size)
{	
	// Create a vertex buffer view.
	view = {
		.BufferLocation = buffer,
		.SizeInBytes = vertex_count * vertex_size,
		.StrideInBytes = vertex_size,
	};

	// Create a descriptor.
	uint64_t first_element = (buffer - resource->GetGPUVirtualAddress()) / vertex_size;
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer(vertex_count, vertex_size);
	this->descriptor = descriptor_allocator->AllocateAndCreateSrv(resource, &srv_desc);
}

void* VertexBuffer::QueueUpdate(UploadBuffer* upload_buffer, ID3D12Resource* resource)
{
	uint64_t offset = this->view.BufferLocation - resource->GetGPUVirtualAddress();
	return upload_buffer->QueueBufferUpload(this->view.SizeInBytes, resource, offset);
}

void VertexBuffer::Destroy(CbvSrvUavPool* descriptor_allocator)
{
	view = {};
	descriptor_allocator->Free(descriptor);
	descriptor = -1;
}

VertexAllocation IndexBuffer::GetAllocationSize(uint32_t index_count, DXGI_FORMAT format)
{
	uint32_t index_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	return {index_count * index_size, index_size};
}

void IndexBuffer::Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, CbvSrvUavPool* descriptor_allocator, uint32_t index_count, DXGI_FORMAT format)
{
	UINT index_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	
	// Create an index buffer view.
	view = {
		.BufferLocation = resource->GetGPUVirtualAddress(),
		.SizeInBytes = index_count * index_size,
		.Format = format,
	};

	// Create a descriptor.
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TypedBuffer(format, index_count);
	this->descriptor = descriptor_allocator->AllocateAndCreateSrv(resource, &srv_desc);
}

void* IndexBuffer::QueueUpdate(UploadBuffer* upload_buffer, ID3D12Resource* resource)
{
	uint64_t offset = this->view.BufferLocation - resource->GetGPUVirtualAddress();
	return upload_buffer->QueueBufferUpload(this->view.SizeInBytes, resource, offset);
}

void IndexBuffer::Destroy(CbvSrvUavPool* descriptor_allocator)
{
	view = {};
	descriptor_allocator->Free(descriptor);
	descriptor = -1;
}

HRESULT Mesh::Create(ID3D12Device* device, CbvSrvUavPool* descriptor_allocator, const Desc* desc, const wchar_t* name)
{
	this->topology = desc->topology;
	this->flags = desc->flags;
	this->num_of_indices = desc->num_of_indices;
	this->num_of_vertices = desc->num_of_vertices;

	// Calculate the space needed.
	uint64_t size = 0;
	VertexAllocation null_allocation = {};
	VertexAllocation allocations[] = {
		desc->flags & FLAG_INDEX ? IndexBuffer::GetAllocationSize(num_of_indices, desc->index_format) : null_allocation,
		VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT),
		desc->flags & FLAG_NORMAL ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT) : null_allocation,
		desc->flags & FLAG_TANGENT ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT) : null_allocation,
		desc->flags & FLAG_TEXCOORD_0 ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32_FLOAT) : null_allocation,
		desc->flags & FLAG_TEXCOORD_1 ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32_FLOAT) : null_allocation,
		desc->flags & FLAG_COLOR ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT) : null_allocation,
		desc->flags & FLAG_JOINT_WEIGHT ? VertexBuffer::GetAllocationSize(num_of_vertices, sizeof(JointWeight)) : null_allocation,
	};
	uint64_t offsets[std::size(allocations)];
	size = CalculateTotalAllocationSize(std::size(allocations), allocations, offsets);
	
	// Allocate a resource for indices and vertices.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	if (result != S_OK) {
		Destroy(descriptor_allocator);
		return result;
	}
	if (name) {
		result = resource->SetName(name);
	} else {
		result = resource->SetName(L"Static Mesh");
	}
	assert(SUCCEEDED(result));

	D3D12_GPU_VIRTUAL_ADDRESS base_address = resource->GetGPUVirtualAddress();
	if (desc->flags & FLAG_INDEX) {
    	index.Create(resource.Get(), base_address + offsets[0], descriptor_allocator, num_of_indices, desc->index_format);
	}
	position.Create(resource.Get(), base_address + offsets[1], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT);
    if (desc->flags & FLAG_NORMAL) {
		normal.Create(resource.Get(), base_address + offsets[2], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT);
	}
    if (desc->flags & FLAG_TANGENT) {
		tangent.Create(resource.Get(), base_address + offsets[3], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT);
	}
    if (desc->flags & FLAG_TEXCOORD_0) {
		texcoords[0].Create(resource.Get(), base_address + offsets[4], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32_FLOAT);
	}
    if (desc->flags & FLAG_TEXCOORD_1) {
		texcoords[1].Create(resource.Get(), base_address + offsets[5], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32_FLOAT);
	}
    if (desc->flags & FLAG_COLOR) {
		color.Create(resource.Get(), base_address + offsets[6], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT);
	}
    if (desc->flags & FLAG_JOINT_WEIGHT) {
		joint_weight.Create(resource.Get(), base_address + offsets[7], descriptor_allocator, num_of_vertices, sizeof(JointWeight));
	}

	return S_OK;
}

void* Mesh::QueueIndexUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_INDEX);
	return index.QueueUpdate(upload_buffer, resource.Get());
}

void* Mesh::QueuePositionUpdate(UploadBuffer* upload_buffer)
{
	return position.QueueUpdate(upload_buffer, resource.Get());
}

void* Mesh::QueueNormalUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_NORMAL);
	return normal.QueueUpdate(upload_buffer, resource.Get());
}

void* Mesh::QueueTangentUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_TANGENT);
	return tangent.QueueUpdate(upload_buffer, resource.Get());
}

void* Mesh::QueueTexcoord0Update(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_TEXCOORD_0);
	return texcoords[0].QueueUpdate(upload_buffer, resource.Get());
}

void* Mesh::QueueTexcoord1Update(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_TEXCOORD_1);
	return texcoords[1].QueueUpdate(upload_buffer, resource.Get());
}

void* Mesh::QueueColorUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_COLOR);
	return color.QueueUpdate(upload_buffer, resource.Get());
}

void* Mesh::QueueJointWeightUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_JOINT_WEIGHT);
	return joint_weight.QueueUpdate(upload_buffer, resource.Get());
}

void Mesh::Destroy(CbvSrvUavPool* descriptor_allocator)
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

HRESULT DynamicMesh::Create(ID3D12Device* device, CbvSrvUavPool* descriptor_allocator, const Desc* desc, const wchar_t* name)
{
	this->flags = desc->flags;
	this->num_of_vertices = desc->num_of_vertices;
	this->current_position_buffer = 0;

	uint64_t size = 0;
	VertexAllocation null_allocation = {};
	VertexAllocation allocations[] = {
		desc->flags & FLAG_POSITION ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT) : null_allocation,
		desc->flags & FLAG_POSITION ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT) : null_allocation,
		desc->flags & FLAG_NORMAL ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT) : null_allocation,
		desc->flags & FLAG_TANGENT ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT) : null_allocation
	};
	uint64_t offsets[std::size(allocations)];
	size = CalculateTotalAllocationSize(std::size(allocations), allocations, offsets);
	
	// Allocate a resource for vertices.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	if (result != S_OK) {
		Destroy(descriptor_allocator);
		return result;
	}
	if (name) {
		result = resource->SetName(name);
	} else {
		result = resource->SetName(L"Dynamic Mesh");
	}
	assert(SUCCEEDED(result));

	D3D12_GPU_VIRTUAL_ADDRESS base_address = resource->GetGPUVirtualAddress();
	if (desc->flags & FLAG_POSITION) {
		position[0].Create(resource.Get(), base_address + offsets[0], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT);
		position[1].Create(resource.Get(), base_address + offsets[1], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT);
	}
    if (desc->flags & FLAG_NORMAL) {
		normal.Create(resource.Get(), base_address + offsets[2], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT);
	}
    if (desc->flags & FLAG_TANGENT) {
		tangent.Create(resource.Get(), base_address + offsets[3], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32A32_FLOAT);
	}

	return S_OK;
}

void DynamicMesh::Destroy(CbvSrvUavPool* descriptor_allocator)
{
	resource.Reset();
	position[0].Destroy(descriptor_allocator);
	position[1].Destroy(descriptor_allocator);
	normal.Destroy(descriptor_allocator);
	tangent.Destroy(descriptor_allocator);
}

void DynamicMesh::Flip()
{
	this->current_position_buffer = (this->current_position_buffer + 1) % 1;
}

VertexBuffer* DynamicMesh::GetCurrentPositionBuffer()
{
	return &position[current_position_buffer];
}

VertexBuffer* DynamicMesh::GetPreviousPositionBuffer()
{
	return &position[(current_position_buffer - 1) % 1];
}

HRESULT MorphTarget::Create(ID3D12Device* device, CbvSrvUavPool* descriptor_allocator, const Desc* desc, const wchar_t* name)
{
	this->flags = desc->flags;
	this->num_of_vertices = desc->num_of_vertices;

	uint64_t size = 0;
	VertexAllocation null_allocation = {};
	VertexAllocation allocations[] = {
		desc->flags & FLAG_POSITION ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT) : null_allocation,
		desc->flags & FLAG_POSITION ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT) : null_allocation,
		desc->flags & FLAG_NORMAL ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT) : null_allocation,
		desc->flags & FLAG_TANGENT ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT) : null_allocation
	};
	uint64_t offsets[std::size(allocations)];
	size = CalculateTotalAllocationSize(std::size(allocations), allocations, offsets);
	
	// Allocate a resource for vertices.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(size);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));
	if (result != S_OK) {
		Destroy(descriptor_allocator);
		return result;
	}
	if (name) {
		result = resource->SetName(name);
	} else {
		result = resource->SetName(L"Morph Target");
	}
	assert(SUCCEEDED(result));

	D3D12_GPU_VIRTUAL_ADDRESS base_address = resource->GetGPUVirtualAddress();
	if (desc->flags & FLAG_POSITION) {
		position.Create(resource.Get(), base_address + offsets[0], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT);
	}
    if (desc->flags & FLAG_NORMAL) {
		normal.Create(resource.Get(), base_address + offsets[1], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT);
	}
    if (desc->flags & FLAG_TANGENT) {
		tangent.Create(resource.Get(), base_address + offsets[2], descriptor_allocator, num_of_vertices, DXGI_FORMAT_R32G32B32_FLOAT);
	}

	return S_OK;
}

void* MorphTarget::QueuePositionUpdate(UploadBuffer* upload_buffer)
{
	return position.QueueUpdate(upload_buffer, resource.Get());
}

void* MorphTarget::QueueNormalUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_NORMAL);
	return normal.QueueUpdate(upload_buffer, resource.Get());
}

void* MorphTarget::QueueTangentUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_TANGENT);
	return tangent.QueueUpdate(upload_buffer, resource.Get());
}

void MorphTarget::Destroy(CbvSrvUavPool* descriptor_allocator)
{
	resource.Reset();
	position.Destroy(descriptor_allocator);
	normal.Destroy(descriptor_allocator);
	tangent.Destroy(descriptor_allocator);
}