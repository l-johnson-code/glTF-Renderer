#pragma once

#include <directx/d3d12.h>
#include <glm/glm.hpp>
#include <wrl/client.h>

#include "DescriptorAllocator.h"
#include "UploadBuffer.h"

struct VertexBuffer {
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	D3D12_VERTEX_BUFFER_VIEW view = {};
	int descriptor = -1;

	void Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t vertex_count, DXGI_FORMAT format, const wchar_t* name);
	void Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t vertex_count, uint32_t element_size, const wchar_t* name);
    void Destroy(DescriptorPool* descriptor_allocator);
    void* QueueUpdate(UploadBuffer* upload_buffer);
};

struct IndexBuffer {
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	D3D12_INDEX_BUFFER_VIEW view = {};
	int descriptor = -1;

	void Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t index_count, DXGI_FORMAT format, const wchar_t* name);
    void Destroy(DescriptorPool* descriptor_allocator);
    void* QueueUpdate(UploadBuffer* upload_buffer);
};

struct DynamicVertexBuffer {
	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	D3D12_VERTEX_BUFFER_VIEW view = {};
	int descriptor = -1;

	void Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t vertex_count, DXGI_FORMAT format, const wchar_t* name);
	void Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, uint32_t vertex_count, uint32_t element_size, const wchar_t* name);
    void Destroy(DescriptorPool* descriptor_allocator);
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
        uint32_t flags;
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
    uint32_t flags = 0;
    uint32_t num_of_vertices = 0;
    uint32_t num_of_indices = 0;

    IndexBuffer index;
    VertexBuffer position;
    VertexBuffer normal;
    VertexBuffer tangent;
    VertexBuffer texcoords[MAX_TEXCOORDS];
    VertexBuffer color;
    VertexBuffer joint_weight;

    void Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, const Desc* description);
    void Destroy(DescriptorPool* descriptor_allocator);
};

struct DynamicMesh {

    struct Desc {
        uint32_t num_of_vertices;
        uint32_t flags;
    };

    enum Flags {
        FLAG_POSITION = 1 << 0,
        FLAG_NORMAL = 1 << 1,
        FLAG_TANGENT = 1 << 2,
    };

    uint32_t flags = 0;
    uint32_t num_of_vertices = 0;
    int current_position_buffer = 0;

    DynamicVertexBuffer position[2];
    DynamicVertexBuffer normal;
    DynamicVertexBuffer tangent;

    void Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, const Desc* description);
    void Destroy(DescriptorPool* descriptor_allocator);
    void Flip();
    DynamicVertexBuffer* GetCurrentPositionBuffer();
    DynamicVertexBuffer* GetPreviousPositionBuffer();
};

struct MorphTarget {

    enum Flags {
        FLAG_POSITION = 1 << 0,
        FLAG_NORMAL = 1 << 1,
        FLAG_TANGENT = 1 << 2,
    };

    struct Desc {
        uint32_t num_of_vertices;
        uint32_t flags;
    };

    uint32_t flags = 0;
    uint32_t num_of_vertices = 0;
    
    VertexBuffer position;
    VertexBuffer normal;
    VertexBuffer tangent;

    void Create(ID3D12Device* device, DescriptorPool* descriptor_allocator, const Desc* attributes);
    void Destroy(DescriptorPool* descriptor_allocator);
};