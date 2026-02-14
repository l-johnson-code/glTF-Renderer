#pragma once

#include <directx/d3d12.h>
#include <glm/glm.hpp>
#include <wrl/client.h>

#include "DescriptorAllocator.h"
#include "UploadBuffer.h"

struct VertexAllocation {
	uint64_t size;
	uint64_t alignment;
};

struct VertexBuffer {
	D3D12_VERTEX_BUFFER_VIEW view = {};
	int descriptor = -1;

    static VertexAllocation GetAllocationSize(uint32_t vertex_count, DXGI_FORMAT format);
    static VertexAllocation GetAllocationSize(uint32_t vertex_count, uint32_t element_size);
	void Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, CbvSrvUavPool* descriptor_allocator, uint32_t vertex_count, DXGI_FORMAT format);
	void Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, CbvSrvUavPool* descriptor_allocator, uint32_t vertex_count, uint32_t vertex_size);
    void* QueueUpdate(UploadBuffer* upload_buffer, ID3D12Resource* resource);
    void Destroy(CbvSrvUavPool* descriptor_allocator);
};

struct IndexBuffer {
	D3D12_INDEX_BUFFER_VIEW view = {};
	int descriptor = -1;

    static VertexAllocation GetAllocationSize(uint32_t index_count, DXGI_FORMAT format);
	void Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, CbvSrvUavPool* descriptor_allocator, uint32_t index_count, DXGI_FORMAT format);
    void* QueueUpdate(UploadBuffer* upload_buffer, ID3D12Resource* resource);
    void Destroy(CbvSrvUavPool* descriptor_allocator);
};

struct Mesh {

    static constexpr int MAX_TEXCOORDS = 2;

    struct JointWeight {
        glm::u32vec4 joints;
        glm::vec4 weights;
    };

    struct Desc {
        D3D12_PRIMITIVE_TOPOLOGY topology;
        DXGI_FORMAT index_format;
        uint32_t num_of_vertices;
        uint32_t num_of_indices;
        uint8_t flags;
    };

    enum Flags {
        FLAG_INDEX = 1 << 0,
        FLAG_NORMAL = 1 << 1,
        FLAG_TANGENT = 1 << 2,
        FLAG_TEXCOORD_0 = 1 << 3,
        FLAG_TEXCOORD_1 = 1 << 4,
        FLAG_COLOR = 1 << 5,
        FLAG_JOINT_WEIGHT = 1 << 6,
    };

    D3D12_PRIMITIVE_TOPOLOGY topology;
    uint8_t flags = 0;
    uint32_t num_of_vertices = 0;
    uint32_t num_of_indices = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;

    IndexBuffer index;
    VertexBuffer position;
    VertexBuffer normal;
    VertexBuffer tangent;
    VertexBuffer texcoords[MAX_TEXCOORDS];
    VertexBuffer color;
    VertexBuffer joint_weight;

    HRESULT Create(ID3D12Device* device, CbvSrvUavPool* descriptor_allocator, const Desc* desc, const char* name = nullptr);
    void* QueueIndexUpdate(UploadBuffer* upload_buffer);
    void* QueuePositionUpdate(UploadBuffer* upload_buffer);
    void* QueueNormalUpdate(UploadBuffer* upload_buffer);
    void* QueueTangentUpdate(UploadBuffer* upload_buffer);
    void* QueueTexcoord0Update(UploadBuffer* upload_buffer);
    void* QueueTexcoord1Update(UploadBuffer* upload_buffer);
    void* QueueColorUpdate(UploadBuffer* upload_buffer);
    void* QueueJointWeightUpdate(UploadBuffer* upload_buffer);
    void Destroy(CbvSrvUavPool* descriptor_allocator);
};

struct DynamicMesh {

    struct Desc {
        uint32_t num_of_vertices;
        uint8_t flags;
    };

    enum Flags {
        FLAG_POSITION = 1 << 0,
        FLAG_NORMAL = 1 << 1,
        FLAG_TANGENT = 1 << 2,
    };

    uint8_t flags = 0;
    uint32_t num_of_vertices = 0;
    int current_position_buffer = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;

    VertexBuffer position[2];
    VertexBuffer normal;
    VertexBuffer tangent;

    HRESULT Create(ID3D12Device* device, CbvSrvUavPool* descriptor_allocator, const Desc* desc, const char* name = nullptr);
    void Flip();
    VertexBuffer* GetCurrentPositionBuffer();
    VertexBuffer* GetPreviousPositionBuffer();
    void Destroy(CbvSrvUavPool* descriptor_allocator);
};

struct MorphTarget {

    enum Flags {
        FLAG_POSITION = 1 << 0,
        FLAG_NORMAL = 1 << 1,
        FLAG_TANGENT = 1 << 2,
    };

    struct Desc {
        uint32_t num_of_vertices;
        uint8_t flags;
    };

    uint8_t flags = 0;
    uint32_t num_of_vertices = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    
    VertexBuffer position;
    VertexBuffer normal;
    VertexBuffer tangent;

    HRESULT Create(ID3D12Device* device, CbvSrvUavPool* descriptor_allocator, const Desc* desc, const char* name = nullptr);
    void* QueuePositionUpdate(UploadBuffer* upload_buffer);
    void* QueueNormalUpdate(UploadBuffer* upload_buffer);
    void* QueueTangentUpdate(UploadBuffer* upload_buffer);
    void Destroy(CbvSrvUavPool* descriptor_allocator);
};