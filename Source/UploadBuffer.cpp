#include "UploadBuffer.h"

#include <cassert>
#include <vector>

#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>
#include <directx/dxgiformat.h>

void UploadBuffer::Create(ID3D12Device* device, size_t capacity, D3D12_COMMAND_QUEUE_PRIORITY command_queue_priority, int max_queued_uploads)
{
	HRESULT result = S_OK;

	// Create a ring buffer.
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_UPLOAD);
	allocator.Create(device, capacity, &heap_properties, D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE, L"Upload Buffer");
	markers = std::vector<uint64_t>(max_queued_uploads, 0);

    // Create command queue, allocators, and list for copying data from the upload resource to the gpu heap.
	D3D12_COMMAND_QUEUE_DESC copy_queue_desc = {
		.Type = D3D12_COMMAND_LIST_TYPE_COPY,
		.Priority = command_queue_priority,
	};
	result = device->CreateCommandQueue(&copy_queue_desc, IID_PPV_ARGS(this->copy_command_queue.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = this->copy_command_queue->SetName(L"Copy Command Queue");
	assert(result == S_OK);

	command_allocators = std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>>(max_queued_uploads);
	for (int i = 0; i < max_queued_uploads; i++) {
		result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(this->command_allocators[i].ReleaseAndGetAddressOf()));
		assert(result == S_OK);
		result = this->command_allocators[i]->SetName(L"Copy Command Allocator");
		assert(result == S_OK);
	}

	result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, this->command_allocators[0].Get(), nullptr, IID_PPV_ARGS(this->command_list.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = this->command_list->SetName(L"Copy Command List");
	assert(result == S_OK);
	result = this->command_list->Close();
	assert(result == S_OK);

	// Create a fence so we can tell when a previous upload has completed.
	this->submission_count = 0;
	result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(this->upload_fence.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	result = this->upload_fence->SetName(L"Upload Fence");
	assert(result == S_OK);

	this->recording = false;
}

uint64_t UploadBuffer::Allocate(uint64_t size, uint64_t alignment)
{
	// If the ring buffer is too small, allocation always fails.
	assert(size <= this->allocator.Capacity());
	uint64_t offset = std::numeric_limits<uint64_t>::max();
	if (size > this->allocator.Capacity()) {
		return offset;
	}

	// Try to allocate.
	offset = this->allocator.Allocate(size, alignment);
	if (offset != std::numeric_limits<uint64_t>::max()) {
		return offset;
	}

	// If we can't allocate the data, then wait for previous requests to complete, freeing up space in the ring buffer.
	for (uint64_t i = this->completed_submissions; i < this->submission_count; i++) {
		WaitForSubmissionToComplete(i);
		allocator.Free(markers[i % markers.size()]);
		this->completed_submissions++;
		offset = this->allocator.Allocate(size, alignment);
		if (offset != std::numeric_limits<uint64_t>::max()) {
			return offset;
		}
	}

	// If all previous requests have completed, submit the current batch, wait for it to complete, and begin a new batch.
	uint64_t submission = Submit();
	WaitForAllSubmissionsToComplete();
	Begin();
	offset = this->allocator.Allocate(size, alignment);
	return offset;
}

void* UploadBuffer::GetCpuAddress(uint64_t offset)
{
	return offset != -1 ? (std::byte*)allocator.GetCpuAddress(0) + offset : nullptr;
}

void UploadBuffer::Begin()
{
	assert(!this->recording);

	HRESULT result = S_OK;

	// Wait for a command allocator to become available.
	uint64_t completed_submissions = upload_fence->GetCompletedValue();
	if ((submission_count - completed_submissions) >= command_allocators.size()) {
		// Wait for a command queue to become available.
		result = upload_fence->SetEventOnCompletion(submission_count - command_allocators.size() + 1, upload_event);
		assert(result == S_OK);
		WaitForSingleObjectEx(upload_event, INFINITE, FALSE);
		completed_submissions++;
	}

	// Free unused memory in the ring buffer.
	for (uint64_t i = this->completed_submissions; i < completed_submissions; i++) {
		allocator.Free(markers[i % markers.size()]);
	}
	this->completed_submissions = completed_submissions;

	// Prepare to record copy commands.
	ID3D12CommandAllocator* command_allocator = command_allocators[submission_count % command_allocators.size()].Get();
	result = command_allocator->Reset();
	assert(result == S_OK);
	result = command_list->Reset(command_allocator, nullptr);
	assert(result == S_OK);

	this->recording = true;
}

uint64_t UploadBuffer::Submit()
{
	assert(this->recording);

	HRESULT result = S_OK;

	// Finalize command list.
	result = command_list->Close();
	assert(result == S_OK);
	
	// Execute command list.
	Microsoft::WRL::ComPtr<ID3D12CommandList> temp;
	result = command_list.As(&temp);
	assert(result == S_OK);
	copy_command_queue->ExecuteCommandLists(1, temp.GetAddressOf());
	assert(result == S_OK);

	this->markers[this->submission_count % markers.size()] = this->allocator.GetMarker();

	// Signal when this upload is done.
	this->submission_count++;
	copy_command_queue->Signal(upload_fence.Get(), this->submission_count);

	this->recording = false;
	return this->submission_count;
}

void UploadBuffer::WaitForSubmissionToComplete(uint64_t submission)
{
	assert(submission <= this->submission_count);
	
	HRESULT result = S_OK;

	// Check if the last resource upload has completed.
	uint64_t completed_value = upload_fence->GetCompletedValue();
	if (submission > completed_value) {
		// Wait for the last resource upload to complete.
		result = upload_fence->SetEventOnCompletion(submission, upload_event);
		assert(result == S_OK);
		WaitForSingleObjectEx(upload_event, INFINITE, FALSE);
	}
}

void UploadBuffer::WaitForAllSubmissionsToComplete()
{
	WaitForSubmissionToComplete(this->submission_count);
}

void* UploadBuffer::QueueBufferUpload(uint64_t size, ID3D12Resource* destination_resource, uint64_t destination_offset)
{
	uint64_t offset = Allocate(size, 0);
	void* pointer = GetCpuAddress(offset);
	command_list->CopyBufferRegion(destination_resource, destination_offset, this->allocator.Resource(), offset, size);
	return pointer;
}

void* UploadBuffer::QueueTextureUpload(DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t depth, ID3D12Resource* destination_resource, int destination_subresource_index, uint32_t* row_pitch)
{
    HRESULT result = S_OK;

	result = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::CalculateMinimumRowMajorRowPitch(format, width, *row_pitch);
	assert(result == S_OK);
	uint64_t allocation_size = (*row_pitch) * height * depth;
	uint64_t offset = Allocate(allocation_size, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	void* pointer = GetCpuAddress(offset);

	CD3DX12_TEXTURE_COPY_LOCATION texture_destination(destination_resource, destination_subresource_index);
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT source_footprint = {
		.Offset = offset,
		.Footprint = CD3DX12_SUBRESOURCE_FOOTPRINT(format, width, height, depth, *row_pitch),
	};
	CD3DX12_TEXTURE_COPY_LOCATION texture_source(allocator.Resource(), source_footprint);

	command_list->CopyTextureRegion(&texture_destination, 0, 0, 0, &texture_source, nullptr);

	return pointer;
}