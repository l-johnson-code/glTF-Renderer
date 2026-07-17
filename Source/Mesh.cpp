#include "Mesh.h"

#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>
#include <glm/gtc/constants.hpp>

#include "Memory.h"
#include "Profiling.h"

static glm::vec2 EncodeOctahedralMap(glm::vec3 normal)
{
	// Project onto the octahedron.
	glm::vec3 octahedral = normal / (glm::abs(normal.x) + glm::abs(normal.y) + glm::abs(normal.z));
	// Flatten onto square with coordinates in range [-1, 1].
	glm::vec2 result;
	if (octahedral.z >= 0.f) {
		result = glm::vec2(octahedral.x, octahedral.y);
	} else {
		result.x = (octahedral.x >= 0.f ? 1.f : -1.f) * (1.f - glm::abs(octahedral.y));
		result.y = (octahedral.y >= 0.f ? 1.f : -1.f) * (1.f - glm::abs(octahedral.x));
	}
	return result;
}

static glm::vec3 DecodeOctahedralMap(glm::vec2 encoded)
{
	glm::vec3 result;
	// Find point on octahedron.
	result.z = 1. - glm::abs(encoded.x) - glm::abs(encoded.y);
	if (result.z >= 0.) {
		result.x = encoded.x;
		result.y = encoded.y;
	} else {
		result.x = (encoded.x >= 0.f ? 1.f : -1.f) * (1.f - glm::abs(encoded.y));
		result.y = (encoded.y >= 0.f ? 1.f : -1.f) * (1.f - glm::abs(encoded.x));
	}
	// Project onto sphere.
	result = glm::normalize(result);
	return result;
}

// From the paper "Building an Orthonormal Basis, Revisited".
static void CreateBasis(glm::vec3 normal, glm::vec3* tangent, glm::vec3* bitangent)
{
	const float sign = normal.z >= 0.0f ? 1.0f : -1.0f;
	const float a = -1.0f / (sign + normal.z);
	const float b = normal.x * normal.y * a;
	*tangent = glm::vec3(1.0f + sign * normal.x * normal.x * a, sign * b, -sign * normal.x);
	*bitangent = glm::vec3(b, sign + normal.y * normal.y * a, -normal.y);
}

uint32_t Mesh::EncodeNormal(glm::vec3 normal)
{
	glm::vec4 encoded;

    // Encode normal.
    glm::vec2 encoded_normal = 0.5f * EncodeOctahedralMap(normal) + 0.5f;
	glm::u32vec2 quantized_normal = glm::clamp(encoded_normal, 0.0f, 1.0f) * 1023.0f + 0.5f;

    // Encode winding.
    uint32_t quantized_winding = 3;

    return quantized_normal.x | (quantized_normal.y << 10) | (quantized_winding << 30);
}

uint32_t Mesh::EncodeTangentSpace(glm::vec3 normal, glm::vec4 tangent)
{
	glm::vec4 encoded;

    // Encode and quantize normal.
    glm::vec2 encoded_normal = 0.5f * EncodeOctahedralMap(normal) + 0.5f;
	glm::u32vec2 quantized_normal = glm::clamp(encoded_normal, 0.0f, 1.0f) * 1023.0f + 0.5f;

	// Decode normal to use in basis calculation.
	// This is to prevent numerical issues due to quantization.
	glm::vec2 unpacked_encoded_normal = glm::vec2(quantized_normal) / 1023.0f;
	normal = DecodeOctahedralMap(2.0f * unpacked_encoded_normal - 1.0f);

    // Encode tangent.
    glm::vec3 canonical_tangent;
    glm::vec3 canonical_bitangent;
    CreateBasis(normal, &canonical_tangent, &canonical_bitangent);
    float angle = std::atan2(glm::dot(glm::vec3(tangent), canonical_bitangent), glm::dot(glm::vec3(tangent), canonical_tangent));
    float encoded_tangent = (angle / glm::two_pi<float>()) + 0.5f;
	uint32_t quantized_tangent = glm::clamp(encoded_tangent, 0.0f, 1.0f) * 1023.0f + 0.5f;

    // Encode winding.
    uint32_t quantized_winding = tangent.w == 1.0f ? 3 : 0;

    return quantized_normal.x | (quantized_normal.y << 10) | (quantized_tangent << 20) | (quantized_winding << 30);
}

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

void VertexBuffer::Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, Gpu::Resources* resources, uint32_t vertex_count, DXGI_FORMAT format)
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
	this->descriptor = resources->CreateShaderResourceView(resource, &srv_desc);
}

void VertexBuffer::Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, Gpu::Resources* resources, uint32_t vertex_count, uint32_t vertex_size)
{	
	// Create a vertex buffer view.
	view = {
		.BufferLocation = buffer,
		.SizeInBytes = vertex_count * vertex_size,
		.StrideInBytes = vertex_size,
	};

	// Create a descriptor.
	uint64_t first_element = (buffer - resource->GetGPUVirtualAddress()) / vertex_size;
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::StructuredBuffer(vertex_count, vertex_size, first_element);
	this->descriptor = resources->CreateShaderResourceView(resource, &srv_desc);
}

void* VertexBuffer::QueueUpdate(UploadBuffer* upload_buffer, ID3D12Resource* resource)
{
	uint64_t offset = this->view.BufferLocation - resource->GetGPUVirtualAddress();
	return upload_buffer->QueueBufferUpload(this->view.SizeInBytes, resource, offset);
}

void VertexBuffer::Destroy(Gpu::Resources* resources)
{
	view = {};
	resources->FreeResourceDescriptor(descriptor);
	descriptor = -1;
}

VertexAllocation IndexBuffer::GetAllocationSize(uint32_t index_count, DXGI_FORMAT format)
{
	uint32_t index_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	return {index_count * index_size, index_size};
}

void IndexBuffer::Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, Gpu::Resources* resources, uint32_t index_count, DXGI_FORMAT format)
{
	UINT index_size = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	
	// Create an index buffer view.
	view = {
		.BufferLocation = buffer,
		.SizeInBytes = index_count * index_size,
		.Format = format,
	};

	// Create a descriptor.
	CD3DX12_SHADER_RESOURCE_VIEW_DESC srv_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TypedBuffer(format, index_count);
	this->descriptor = resources->CreateShaderResourceView(resource, &srv_desc);
}

void* IndexBuffer::QueueUpdate(UploadBuffer* upload_buffer, ID3D12Resource* resource)
{
	uint64_t offset = this->view.BufferLocation - resource->GetGPUVirtualAddress();
	return upload_buffer->QueueBufferUpload(this->view.SizeInBytes, resource, offset);
}

void IndexBuffer::Destroy(Gpu::Resources* resources)
{
	view = {};
	resources->FreeResourceDescriptor(descriptor);
	descriptor = -1;
}

HRESULT Mesh::Create(Gpu::Resources* resources, const Desc* desc, const char* name)
{
	ProfileZoneScoped();
	this->topology = desc->topology;
	this->flags = desc->flags;
	this->num_of_indices = desc->num_of_indices;
	this->num_of_vertices = desc->num_of_vertices;

	// Calculate the space needed.
	uint64_t size = 0;
	VertexAllocation null_allocation = {};
	VertexAllocation allocations[] = {
		desc->flags & FLAG_INDEX ? IndexBuffer::GetAllocationSize(num_of_indices, desc->index_format) : null_allocation,
		VertexBuffer::GetAllocationSize(num_of_vertices, sizeof(PositionAndTangentSpace)),
		desc->flags & FLAG_TEXCOORD ? VertexBuffer::GetAllocationSize(num_of_vertices, sizeof(glm::vec2)) : null_allocation,
		desc->flags & FLAG_COLOR ? VertexBuffer::GetAllocationSize(num_of_vertices, DXGI_FORMAT_R16G16B16A16_UNORM) : null_allocation,
		desc->flags & FLAG_JOINT_WEIGHT ? VertexBuffer::GetAllocationSize(num_of_vertices, sizeof(JointWeight)) : null_allocation,
	};
	uint64_t offsets[std::size(allocations)];
	size = CalculateTotalAllocationSize(std::size(allocations), allocations, offsets);
	
	// Allocate a buffer for indices and vertices.
	Gpu::BufferDesc buffer_desc = {
		.name = name ? name : "Mesh",
		.size = size,
	};
	HRESULT result = resources->CreateBuffer(&buffer_desc, &this->buffer);
	if (result != S_OK) {
		Destroy(resources);
		return result;
	}

	D3D12_GPU_VIRTUAL_ADDRESS base_address = buffer.Resource()->GetGPUVirtualAddress();
	if (desc->flags & FLAG_INDEX) {
    	index.Create(buffer.Resource(), base_address + offsets[0], resources, num_of_indices, desc->index_format);
	}
	position_and_tangent_space.Create(buffer.Resource(), base_address + offsets[1], resources, num_of_vertices, sizeof(PositionAndTangentSpace));
    if (desc->flags & FLAG_TEXCOORD) {
		texcoords.Create(buffer.Resource(), base_address + offsets[2], resources, num_of_vertices, sizeof(glm::vec2));
	}
    if (desc->flags & FLAG_COLOR) {
		color.Create(buffer.Resource(), base_address + offsets[3], resources, num_of_vertices, DXGI_FORMAT_R16G16B16A16_UNORM);
	}
    if (desc->flags & FLAG_JOINT_WEIGHT) {
		joint_weight.Create(buffer.Resource(), base_address + offsets[4], resources, num_of_vertices, sizeof(JointWeight));
	}

	return S_OK;
}

void* Mesh::QueueIndexUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_INDEX);
	return index.QueueUpdate(upload_buffer, buffer.Resource());
}

void* Mesh::QueuePositionAndTangentSpaceUpdate(UploadBuffer* upload_buffer)
{
	return position_and_tangent_space.QueueUpdate(upload_buffer, buffer.Resource());
}

void* Mesh::QueueTexcoordUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_TEXCOORD);
	return texcoords.QueueUpdate(upload_buffer, buffer.Resource());
}

void* Mesh::QueueColorUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_COLOR);
	return color.QueueUpdate(upload_buffer, buffer.Resource());
}

void* Mesh::QueueJointWeightUpdate(UploadBuffer* upload_buffer)
{
	assert(flags & FLAG_JOINT_WEIGHT);
	return joint_weight.QueueUpdate(upload_buffer, buffer.Resource());
}

void Mesh::Destroy(Gpu::Resources* resources)
{
	resources->FreeBuffer(&this->buffer);
	index.Destroy(resources);
	position_and_tangent_space.Destroy(resources);
	texcoords.Destroy(resources);
	color.Destroy(resources);
	joint_weight.Destroy(resources);
}

HRESULT DynamicMesh::Create(Gpu::Resources* resources, const Desc* desc, const char* name)
{
	this->flags = desc->flags;
	this->num_of_vertices = desc->num_of_vertices;
	this->current_position_buffer = 0;

	uint64_t size = 0;
	VertexAllocation null_allocation = {};
	VertexAllocation allocations[] = {
		desc->flags & FLAG_POSITION ? VertexBuffer::GetAllocationSize(num_of_vertices, sizeof(Mesh::PositionAndTangentSpace)) : null_allocation,
		desc->flags & FLAG_POSITION ? VertexBuffer::GetAllocationSize(num_of_vertices, sizeof(Mesh::PositionAndTangentSpace)) : null_allocation,
		desc->flags & FLAG_SKELETON ? VertexBuffer::GetAllocationSize(desc->bone_count, sizeof(glm::mat4x4) * 2) : null_allocation, 
		desc->flags & FLAG_SKELETON ? VertexBuffer::GetAllocationSize(desc->bone_count, sizeof(glm::mat4x4) * 2) : null_allocation, 
	};
	uint64_t offsets[std::size(allocations)];
	size = CalculateTotalAllocationSize(std::size(allocations), allocations, offsets);
	
	// Allocate a buffer for vertices.
	Gpu::BufferDesc buffer_desc = {
		.name = name ? name : "Dynamic Mesh",
		.size = size,
		.flags = Gpu::BUFFER_FLAG_UAV | (desc->flags & FLAG_SKELETON ? Gpu::BUFFER_FLAG_PERSISTENT_MAP : Gpu::BUFFER_FLAG_NONE),
		.heap_type = desc->flags & FLAG_SKELETON ? D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_DEFAULT,
	};
	HRESULT result = resources->CreateBuffer(&buffer_desc, &this->buffer);
	if (result != S_OK) {
		Destroy(resources);
		return result;
	}

	D3D12_GPU_VIRTUAL_ADDRESS base_address = buffer.Resource()->GetGPUVirtualAddress();
	if (desc->flags & FLAG_POSITION) {
		position_and_tangent_space[0].Create(buffer.Resource(), base_address + offsets[0], resources, num_of_vertices, sizeof(Mesh::PositionAndTangentSpace));
		position_and_tangent_space[1].Create(buffer.Resource(), base_address + offsets[1], resources, num_of_vertices, sizeof(Mesh::PositionAndTangentSpace));
	}
	if (desc->flags & FLAG_SKELETON) {
		CD3DX12_UNORDERED_ACCESS_VIEW_DESC bones_uav_desc[2] = {
			CD3DX12_UNORDERED_ACCESS_VIEW_DESC::StructuredBuffer(desc->bone_count, sizeof(glm::mat4x4) * 2, offsets[2] / (sizeof(glm::mat4x4) * 2)),
			CD3DX12_UNORDERED_ACCESS_VIEW_DESC::StructuredBuffer(desc->bone_count, sizeof(glm::mat4x4) * 2, offsets[3] / (sizeof(glm::mat4x4) * 2))
		};
		bone_descriptors[0] = resources->CreateUnorderedAccessView(buffer.Resource(), &bones_uav_desc[0]);
		bone_descriptors[1] = resources->CreateUnorderedAccessView(buffer.Resource(), &bones_uav_desc[1]);
		bone_pointers[0] = (std::byte*)this->buffer.Pointer() + offsets[2];
		bone_pointers[1] = (std::byte*)this->buffer.Pointer() + offsets[3];
	}

	return S_OK;
}

void DynamicMesh::Destroy(Gpu::Resources* resources)
{
	resources->FreeBuffer(&this->buffer);
	position_and_tangent_space[0].Destroy(resources);
	position_and_tangent_space[1].Destroy(resources);
}

void DynamicMesh::Flip()
{
	this->current_position_buffer = (this->current_position_buffer + 1) % 1;
}

VertexBuffer* DynamicMesh::GetCurrentPositionAndTangentSpaceBuffer()
{
	return &position_and_tangent_space[current_position_buffer];
}

VertexBuffer* DynamicMesh::GetPreviousPositionAndTangentSpaceBuffer()
{
	return &position_and_tangent_space[(current_position_buffer - 1) % 1];
}

int DynamicMesh::GetCurrentBoneDescriptor()
{
	assert(this->flags & FLAG_SKELETON);
	return bone_descriptors[current_position_buffer];
}

void* DynamicMesh::GetCurrentBonePointer()
{
	assert(this->flags & FLAG_SKELETON);
	return bone_pointers[current_position_buffer];
}

HRESULT MorphTarget::Create(Gpu::Resources* resources, const Desc* desc, const char* name)
{
	this->flags = desc->flags;
	this->num_of_vertices = desc->num_of_vertices;

	uint64_t size = 0;
	VertexAllocation null_allocation = {};
	VertexAllocation allocations[] = {
		VertexBuffer::GetAllocationSize(num_of_vertices, sizeof(Mesh::PositionAndTangentSpace)),
	};
	uint64_t offsets[std::size(allocations)];
	size = CalculateTotalAllocationSize(std::size(allocations), allocations, offsets);
	
	// Allocate a resource for vertices.
	Gpu::BufferDesc buffer_desc = {
		.name = name ? name : "Morph Target",
		.size = size,
	};
	HRESULT result = resources->CreateBuffer(&buffer_desc, &this->buffer);
	if (result != S_OK) {
		Destroy(resources);
		return result;
	}

	D3D12_GPU_VIRTUAL_ADDRESS base_address = buffer.Resource()->GetGPUVirtualAddress();
	position_and_tangent_space.Create(buffer.Resource(), base_address + offsets[0], resources, num_of_vertices, sizeof(Mesh::PositionAndTangentSpace));

	return S_OK;
}

void* MorphTarget::QueuePositionAndTangentSpaceUpdate(UploadBuffer* upload_buffer)
{
	return position_and_tangent_space.QueueUpdate(upload_buffer, buffer.Resource());
}

void MorphTarget::Destroy(Gpu::Resources* resources)
{
	resources->FreeBuffer(&this->buffer);
	position_and_tangent_space.Destroy(resources);
}