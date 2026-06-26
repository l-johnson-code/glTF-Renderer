#pragma once

#include <directx/d3d12.h>
#include <glm/glm.hpp>
#include <wrl/client.h>

#include "GpuResources.h"
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
	void Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, Gpu::Resources* resources, uint32_t vertex_count, DXGI_FORMAT format);
	void Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, Gpu::Resources* resources, uint32_t vertex_count, uint32_t vertex_size);
    void* QueueUpdate(UploadBuffer* upload_buffer, ID3D12Resource* resource);
    void Destroy(Gpu::Resources* resources);
};

struct IndexBuffer {
	D3D12_INDEX_BUFFER_VIEW view = {};
	int descriptor = -1;

    static VertexAllocation GetAllocationSize(uint32_t index_count, DXGI_FORMAT format);
	void Create(ID3D12Resource* resource, D3D12_GPU_VIRTUAL_ADDRESS buffer, Gpu::Resources* resources, uint32_t index_count, DXGI_FORMAT format);
    void* QueueUpdate(UploadBuffer* upload_buffer, ID3D12Resource* resource);
    void Destroy(Gpu::Resources* resources);
};

struct Mesh {

    static constexpr int MAX_TEXCOORDS = 2;

    struct PositionAndTangentSpace {
        glm::vec3 position;
        glm::uint32_t encoded_tangent_space;
    };

    struct JointWeight {
        glm::u16vec4 joints;
        glm::u16vec4 weights;
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
        FLAG_TANGENT_SPACE = 1 << 1,
        FLAG_TEXCOORD_0 = 1 << 2,
        FLAG_TEXCOORD_1 = 1 << 3,
        FLAG_COLOR = 1 << 4,
        FLAG_JOINT_WEIGHT = 1 << 5,
    };

    D3D12_PRIMITIVE_TOPOLOGY topology;
    uint8_t flags = 0;
    uint32_t num_of_vertices = 0;
    uint32_t num_of_indices = 0;

    Gpu::Buffer buffer;

    IndexBuffer index;
    VertexBuffer position_and_tangent_space;
    VertexBuffer texcoords[MAX_TEXCOORDS];
    VertexBuffer color;
    VertexBuffer joint_weight;

    static uint32_t EncodeNormal(glm::vec3 normal);
    static uint32_t EncodeTangentSpace(glm::vec3 normal, glm::vec4 tangent);

    HRESULT Create(Gpu::Resources* resources, const Desc* desc, const char* name = nullptr);
    void* QueueIndexUpdate(UploadBuffer* upload_buffer);
    void* QueuePositionAndTangentSpaceUpdate(UploadBuffer* upload_buffer);
    void* QueueTexcoord0Update(UploadBuffer* upload_buffer);
    void* QueueTexcoord1Update(UploadBuffer* upload_buffer);
    void* QueueColorUpdate(UploadBuffer* upload_buffer);
    void* QueueJointWeightUpdate(UploadBuffer* upload_buffer);
    void Destroy(Gpu::Resources* resources);
};

struct DynamicMesh {

    struct Desc {
        uint32_t num_of_vertices;
        uint16_t bone_count;
        uint8_t flags;
    };

    enum Flags {
        FLAG_POSITION = 1 << 0,
        FLAG_TANGENT_SPACE = 1 << 1,
        FLAG_SKELETON = 1 << 2,
    };

    uint8_t flags = 0;
    uint32_t num_of_vertices = 0;
    uint16_t bone_count = 0;
    int current_position_buffer = 0;

    Gpu::Buffer buffer;

    VertexBuffer position_and_tangent_space[2];
    int bone_descriptors[2] = {-1, -1};
    void* bone_pointers[2] = {nullptr, nullptr};

    HRESULT Create(Gpu::Resources* resources, const Desc* desc, const char* name = nullptr);
    void Flip();
    VertexBuffer* GetCurrentPositionAndTangentSpaceBuffer();
    VertexBuffer* GetPreviousPositionAndTangentSpaceBuffer();
    int GetCurrentBoneDescriptor();
    void* GetCurrentBonePointer();
    void Destroy(Gpu::Resources* resources);
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

    Gpu::Buffer buffer;
    
    VertexBuffer position_and_tangent_space;

    HRESULT Create(Gpu::Resources* resources, const Desc* desc, const char* name = nullptr);
    void* QueuePositionAndTangentSpaceUpdate(UploadBuffer* upload_buffer);
    void Destroy(Gpu::Resources* resources);
};