#pragma once

#include <vector>

#include <directx/d3d12.h>
#include <wrl/client.h>

#include "BufferAllocator.h"

class UploadBuffer {

    public:

    void Create(ID3D12Device* device, size_t capacity, D3D12_COMMAND_QUEUE_PRIORITY command_queue_priority, int max_queued_uploads);
    void Begin();
    void* QueueBufferUpload(uint64_t size, ID3D12Resource* destination_resource, uint64_t destination_offset);
    void* QueueTextureUpload(DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t depth, ID3D12Resource* destination_resource, int destination_subresource_index, uint32_t* row_pitch);
    uint64_t Submit();
    void WaitForSubmissionToComplete(uint64_t submission);
    void WaitForAllSubmissionsToComplete();
    
    private:
    
    // Allocator.
    CircularBuffer allocator;
    std::vector<uint64_t> markers;
    
    // Command submission.
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> copy_command_queue;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list;
	std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> command_allocators;
    
    // Synchonisation.
	Microsoft::WRL::ComPtr<ID3D12Fence> upload_fence;
	uint64_t submission_count = 0;
    uint64_t completed_submissions = 0;
	HANDLE upload_event = nullptr;
    
    bool recording = false;
    
    uint64_t Allocate(uint64_t size, uint64_t alignment);
    void* GetCpuAddress(uint64_t offset);
};
