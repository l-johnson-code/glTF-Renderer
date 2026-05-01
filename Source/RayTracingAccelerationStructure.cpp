#include "RayTracingAccelerationStructure.h"

#include <cassert>

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <spdlog/spdlog.h>

#include "Memory.h"

void RaytracingAccelerationStructure::Init(ID3D12Device5* device, Gpu::Resources* resources, uint32_t max_blas_vertices, uint32_t max_tlas_instances)
{
	HRESULT result = S_OK;

	this->device = device;
	this->resources = resources;
	this->max_tlas_instances = max_tlas_instances;

	// Calculate size needed for worst case BLAS.
	D3D12_RAYTRACING_GEOMETRY_DESC geometry = {
		.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
		.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
		.Triangles = {
			.IndexFormat = DXGI_FORMAT_R32_UINT,
			.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
			.IndexCount = max_blas_vertices, // Worst case model where there is an index for each vertex.
			.VertexCount = max_blas_vertices,
		}
	};

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = &geometry,
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blas_prebuild_info = {};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&blas_inputs, &blas_prebuild_info);
	max_blas_scratch_size = Align(blas_prebuild_info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

	// Create scratch buffer for BLAS.
	Gpu::BufferDesc blas_scratch_desc = {
		.size = max_blas_scratch_size,
		.flags = Gpu::BUFFER_FLAG_UAV,
		.name = "BLAS Scratch",
	};
	result = blas_scratch.Create(resources, &blas_scratch_desc);
	assert(SUCCEEDED(result));

	// Create heaps for staging TLAS.
	const int instance_desc_stride = Align(sizeof(D3D12_RAYTRACING_INSTANCE_DESC), 16);
	for (int i = 0; i < tlas_staging.Size(); i++) {
		Gpu::BufferDesc tlas_scratch_desc = {
			.size = instance_desc_stride * max_tlas_instances,
			.flags = (Gpu::BufferFlags)(Gpu::BUFFER_FLAG_UAV | Gpu::BUFFER_FLAG_PERSISTENT_MAP),
			.heap_type = resources->allocator.SupportsGpuUploadHeap() ? D3D12_HEAP_TYPE_GPU_UPLOAD : D3D12_HEAP_TYPE_UPLOAD,
			.name = "TLAS Staging",
		};
		tlas_staging[i].Create(resources, &tlas_scratch_desc);
	}

	// Calculate size needed for heaps.
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlas_inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
		.NumDescs = max_tlas_instances,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = 0,
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlas_prebuild_info = {};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&tlas_inputs, &tlas_prebuild_info);

	// Create scratch buffer for building the TLAS.
	Gpu::BufferDesc tlas_scratch_desc = {
		.size = tlas_prebuild_info.ScratchDataSizeInBytes,
		.flags = Gpu::BUFFER_FLAG_UAV,
		.name = "TLAS Scratch",
	};
	result = resources->CreateBuffer(&tlas_scratch_desc, &this->tlas_scratch);
	assert(result == S_OK);

	// Create buffer to store the TLAS.
	Gpu::BufferDesc tlas_desc = {
		.size = tlas_prebuild_info.ResultDataMaxSizeInBytes,
		.flags = Gpu::BUFFER_FLAG_RAYTRACING_ACCELERATION_STRUCTURE,
		.name = "TLAS",
	};
	result = resources->CreateBuffer(&tlas_desc, &this->tlas);
	assert(result == S_OK);
}

void RaytracingAccelerationStructure::BuildStaticBlas(ID3D12GraphicsCommandList4* command_list, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, Blas* blas)
{
	BuildBlas(command_list, vertices, num_of_vertices, indices, num_of_indices, &blas->buffer);
}

void RaytracingAccelerationStructure::BuildDynamicBlas(ID3D12GraphicsCommandList4* command_list, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, DynamicBlas* blas)
{
	BuildBlas(command_list, vertices, num_of_vertices, indices, num_of_indices, &blas->buffer, &blas->update_scratch_size);
}

void RaytracingAccelerationStructure::UpdateDynamicBlas(ID3D12GraphicsCommandList4* command_list, DynamicBlas* blas, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geometry = {
		.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES,
		.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
		.Triangles = {
			.Transform3x4 = 0,
			.IndexFormat = indices.Format,
			.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
			.IndexCount = num_of_indices,
			.VertexCount = num_of_vertices,
			.IndexBuffer = indices.BufferLocation,
			.VertexBuffer = {
				.StartAddress = vertices,
				.StrideInBytes = sizeof(glm::vec3),
			}
		}
	};
	
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = &geometry,
	};

	if (blas_scratch.Capacity() < blas->update_scratch_size) {
		SPDLOG_INFO("BLAS update scratch size exceeded maximum BLAS scratch size.");
		return;
	}

	D3D12_GPU_VIRTUAL_ADDRESS scratch = blas_scratch.Allocate(blas->update_scratch_size, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	if (scratch == 0) {
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(this->blas_scratch.buffer.Resource());
		command_list->ResourceBarrier(1, &barrier);
		this->blas_scratch.Reset();
		scratch = blas_scratch.Allocate(blas->update_scratch_size, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	}

	// Create raytracing acceleration structure.
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC acceleration = {
		.DestAccelerationStructureData = blas->buffer.Resource()->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.SourceAccelerationStructureData = blas->buffer.Resource()->GetGPUVirtualAddress(),
		.ScratchAccelerationStructureData = scratch,
	};
	command_list->BuildRaytracingAccelerationStructure(&acceleration, 0, nullptr);
}

void RaytracingAccelerationStructure::EndBlasBuilds(ID3D12GraphicsCommandList4* command_list)
{
	// Final barrier.
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(this->blas_scratch.buffer.Resource());
	command_list->ResourceBarrier(1, &barrier);
	this->blas_scratch.Reset();
}

void RaytracingAccelerationStructure::BeginTlasBuild()
{
	// Reset the staging buffer.
	instance_count = 0;
	CpuMappedLinearBuffer* staging = &tlas_staging.Current();
	staging->Reset();
}

bool RaytracingAccelerationStructure::AddTlasInstance(Blas* blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags)
{
	if (!blas->buffer.Resource()) {
		SPDLOG_INFO("BLAS was empty.");
		return false;
	}
	return AddTlasInstance(blas->buffer.Resource()->GetGPUVirtualAddress(), transform, instance_mask, flags);
}

bool RaytracingAccelerationStructure::AddTlasInstance(DynamicBlas* blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags)
{
	if (!blas->buffer.Resource()) {
		SPDLOG_INFO("BLAS was empty.");
		return false;
	}
	return AddTlasInstance(blas->buffer.Resource()->GetGPUVirtualAddress(), transform, instance_mask, flags);
}

void RaytracingAccelerationStructure::BuildTlas(ID3D12GraphicsCommandList4* command_list)
{
	CpuMappedLinearBuffer* staging = &tlas_staging.Current();

	// Create top level acceleration structure.
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top_level_inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL,
		.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE,
		.NumDescs = instance_count,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.InstanceDescs = staging->buffer.Resource()->GetGPUVirtualAddress(),
	};

	// Create top level raytracing acceleration structure.
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_acceleration = {
		.DestAccelerationStructureData = tlas.Resource()->GetGPUVirtualAddress(),
		.Inputs = top_level_inputs,
		.ScratchAccelerationStructureData = tlas_scratch.Resource()->GetGPUVirtualAddress(),
	};
	command_list->BuildRaytracingAccelerationStructure(&top_level_acceleration, 0, nullptr);

	// Insert barrier so that we don't use the tlas before its built.
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(tlas.Resource());
	command_list->ResourceBarrier(1, &barrier);

	// Advance the frame so that we dont overwrite the staging buffer while it's being used by the previous frame.
	tlas_staging.Next();
}

D3D12_GPU_VIRTUAL_ADDRESS RaytracingAccelerationStructure::GetAccelerationStructure()
{
	return tlas.Resource()->GetGPUVirtualAddress();
}

void RaytracingAccelerationStructure::BuildBlas(ID3D12GraphicsCommandList4* command_list, D3D12_GPU_VIRTUAL_ADDRESS vertices, uint32_t num_of_vertices, D3D12_INDEX_BUFFER_VIEW indices, uint32_t num_of_indices, Gpu::Buffer* blas_buffer, uint64_t* update_scratch_size)
{
	D3D12_RAYTRACING_GEOMETRY_DESC geometry = {
		.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES, 
		.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE,
		.Triangles = {
			.Transform3x4 = 0,
			.IndexFormat = indices.Format,
			.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT,
			.IndexCount = num_of_indices,
			.VertexCount = num_of_vertices,
			.IndexBuffer = indices.BufferLocation,
			.VertexBuffer = {
				.StartAddress = vertices,
				.StrideInBytes = sizeof(glm::vec3),
			}
		}
	};
	
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {
		.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL,
		.Flags = update_scratch_size ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE,
		.NumDescs = 1,
		.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY,
		.pGeometryDescs = &geometry,
	};

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info = {};
	device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild_info);
	if (update_scratch_size) {
		*update_scratch_size = prebuild_info.UpdateScratchDataSizeInBytes;
	}

	Gpu::BufferDesc blas_desc = {
		.size = prebuild_info.ResultDataMaxSizeInBytes,
		.flags = Gpu::BUFFER_FLAG_UAV,
		.initial_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		.name = "BLAS",
	};
	HRESULT result = resources->CreateBuffer(&blas_desc, blas_buffer);
	assert(result == S_OK);

	if (blas_scratch.Capacity() < prebuild_info.ScratchDataSizeInBytes) {
		SPDLOG_INFO("BLAS build scratch size exceeded maximum BLAS scratch size.");
		return;
	}
	
	D3D12_GPU_VIRTUAL_ADDRESS scratch = blas_scratch.Allocate(prebuild_info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	
	// Check if there was enough space left in the scratch buffer. If there wasn't, insert a barrier and reset the allocator.
	if (scratch == 0) {
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(this->blas_scratch.buffer.Resource());
		command_list->ResourceBarrier(1, &barrier);
		this->blas_scratch.Reset();
		scratch = blas_scratch.Allocate(prebuild_info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
	}

	// Create the BLAS.
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC acceleration = {
		.DestAccelerationStructureData = blas_buffer->Resource()->GetGPUVirtualAddress(),
		.Inputs = inputs,
		.ScratchAccelerationStructureData = scratch,
	};
	command_list->BuildRaytracingAccelerationStructure(&acceleration, 0, nullptr);
}

bool RaytracingAccelerationStructure::AddTlasInstance(D3D12_GPU_VIRTUAL_ADDRESS blas, glm::mat4x4 transform, uint32_t instance_mask, uint32_t flags)
{
	if (instance_count >= max_tlas_instances) {
		SPDLOG_INFO("Max TLAS instances reached.");
		return false;
	}

	// Create the instance.
	D3D12_RAYTRACING_INSTANCE_DESC instance_desc = {
		.Transform = {
			{ transform[0][0], transform[1][0], transform[2][0], transform[3][0] },
			{ transform[0][1], transform[1][1], transform[2][1], transform[3][1] },
			{ transform[0][2], transform[1][2], transform[2][2], transform[3][2] },
		},
		.InstanceID = 1,
		.InstanceMask = instance_mask,
		.InstanceContributionToHitGroupIndex = 0,
		.Flags = flags,
		.AccelerationStructure = blas,
	};

	// Copy to the staging area.
	CpuMappedLinearBuffer* staging = &tlas_staging.Current();
	staging->Copy(&instance_desc, 16);
	instance_count++;
	return true;
}