#pragma once

#include <cassert>

#include <directx/d3d12.h>
#include <glm/glm.hpp>
#include <wrl.h>

#include "BufferAllocator.h"
#include "Memory.h"
#include "UploadBuffer.h"

struct ShaderTableCollection {
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE ray_generation_shader_record;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE miss_shader_table;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE hit_group_table;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE callable_shader_table;
};

static constexpr int SHADER_IDENTIFIER_SIZE = 32;

class ShaderRecordBuilder {
    public:

    static int CalculateRequiredSize()
    {
        return Align(SHADER_IDENTIFIER_SIZE, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    }

    void Create(void* data)
    {
        this->data = data;
    }

    void SetShader(void* shader_identifier) {
        assert(data);
        assert(shader_identifier);
        if (shader_identifier) {
            memcpy(data, shader_identifier, SHADER_IDENTIFIER_SIZE);
        } else {
            memset(data, 0, SHADER_IDENTIFIER_SIZE);
        }
    }

    int Size()
    {
        return Align(SHADER_IDENTIFIER_SIZE, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    }

    private:
    void* data;
};

class ShaderTableBuilder
{
    public:

    static int CalculateRequiredSize(int record_count)
    {
        return record_count * CalculateStride();
    }

    void Create(void* data, int record_count)
    {
        this->data = data;
        this->record_count = record_count;
        this->stride = CalculateStride();
    }

    void SetShader(int record_index, void* shader_identifier)
    {
        assert(data);
        assert(shader_identifier);
        assert((record_index >= 0) && (record_index < record_count));
        if (shader_identifier) {
            memcpy((std::byte*)data + Stride() * record_index, shader_identifier, SHADER_IDENTIFIER_SIZE);
        } else {
            memset((std::byte*)data + Stride() * record_index, 0, SHADER_IDENTIFIER_SIZE);
        }
    }

    int Size()
    {
        return this->record_count * this->stride;
    }

    int RecordCount()
    {
        return this->record_count;
    }

    int Stride()
    {
        return this->stride;
    }

    private:
    void* data = nullptr;
    int record_count = 0;
    int stride = 0;

    static int CalculateStride()
    {
        return Align(SHADER_IDENTIFIER_SIZE, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
    }
};

class ShaderTableCollectionBuilder {
    public:
    
    ShaderRecordBuilder ray_generation_record;
    ShaderTableBuilder miss_table;
    ShaderTableBuilder hit_group_table;
    ShaderTableBuilder callable_table;
    
    static int CalculateRequiredSize(int miss_count, int hit_group_count, int callable_count) {
        Allocation allocations[4] = {
            {ShaderRecordBuilder::CalculateRequiredSize(), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
            {ShaderTableBuilder::CalculateRequiredSize(miss_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
            {ShaderTableBuilder::CalculateRequiredSize(hit_group_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
            {ShaderTableBuilder::CalculateRequiredSize(callable_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
        };
        return CalculateGroupedAllocationSize(allocations, 4);
    }

    void Create(void* data, int miss_count, int hit_group_count, int callable_count)
    {
        std::byte* base = (std::byte*)data;

        Allocation allocations[4] = {
            {ShaderRecordBuilder::CalculateRequiredSize(), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
            {ShaderTableBuilder::CalculateRequiredSize(miss_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
            {ShaderTableBuilder::CalculateRequiredSize(hit_group_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
            {ShaderTableBuilder::CalculateRequiredSize(callable_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
        };

        int offsets[4];
        CalculateGroupedAllocationSizeAndOffsets(allocations, 4, offsets);
        this->miss_table_offset = offsets[1];
        this->hit_group_table_offset = offsets[2];
        this->callable_table_offset = offsets[3];

        this->ray_generation_record.Create(base + offsets[0]);
        this->miss_table.Create(base + offsets[1], miss_count);
        this->hit_group_table.Create(base + offsets[2], hit_group_count);
        this->callable_table.Create(base + offsets[3], callable_count);
    }

    ShaderTableCollection GetShaderTableCollection(D3D12_GPU_VIRTUAL_ADDRESS base_address)
    {
        ShaderTableCollection shader_table_collection = {};

        shader_table_collection.ray_generation_shader_record.StartAddress = base_address;
        shader_table_collection.ray_generation_shader_record.SizeInBytes = this->ray_generation_record.Size();

        if (miss_table.Size()) {
            shader_table_collection.miss_shader_table.StartAddress = base_address + miss_table_offset;
            shader_table_collection.miss_shader_table.StrideInBytes = miss_table.Stride();
            shader_table_collection.miss_shader_table.SizeInBytes = miss_table.Size();
        } else {
            shader_table_collection.miss_shader_table = {0, 0, 0};
        }

        if (hit_group_table.Size()) {
            shader_table_collection.hit_group_table.StartAddress = base_address + hit_group_table_offset;
            shader_table_collection.hit_group_table.StrideInBytes = hit_group_table.Stride();
            shader_table_collection.hit_group_table.SizeInBytes = hit_group_table.Size();
        } else {
            shader_table_collection.hit_group_table = {0, 0, 0};
        }

        if (callable_table.Size()) {
            shader_table_collection.callable_shader_table.StartAddress = base_address + callable_table_offset;
            shader_table_collection.callable_shader_table.StrideInBytes = callable_table.Stride();
            shader_table_collection.callable_shader_table.SizeInBytes = callable_table.Size();
        } else {
            shader_table_collection.callable_shader_table = {0, 0, 0};
        }

        return shader_table_collection;
    }

    private:
    int miss_table_offset = 0;
    int hit_group_table_offset = 0;
    int callable_table_offset = 0;
};

class Pathtracer {

    public:

    enum DebugOutput {
        DEBUG_OUTPUT_NONE,
        DEBUG_OUTPUT_HIT_KIND,
        DEBUG_OUTPUT_VERTEX_COLOR,
        DEBUG_OUTPUT_VERTEX_ALPHA,
        DEBUG_OUTPUT_VERTEX_NORMAL,
        DEBUG_OUTPUT_VERTEX_TANGENT,
        DEBUG_OUTPUT_VERTEX_BITANGENT,
        DEBUG_OUTPUT_TEXCOORD_0,
        DEBUG_OUTPUT_TEXCOORD_1,
        DEBUG_OUTPUT_COLOR,
        DEBUG_OUTPUT_ALPHA,
        DEBUG_OUTPUT_SHADING_NORMAL,
        DEBUG_OUTPUT_SHADING_TANGENT,
        DEBUG_OUTPUT_SHADING_BITANGENT,
        DEBUG_OUTPUT_METALNESS,
        DEBUG_OUTPUT_ROUGHNESS,
        DEBUG_OUTPUT_SPECULAR,
        DEBUG_OUTPUT_SPECULAR_COLOR,
        DEBUG_OUTPUT_CLEARCOAT,
        DEBUG_OUTPUT_CLEARCOAT_ROUGHNESS,
        DEBUG_OUTPUT_CLEARCOAT_NORMAL,
        DEBUG_OUTPUT_TRANSMISSIVE,
        DEBUG_OUTPUT_BOUNCE_DIRECTION,
        DEBUG_OUTPUT_BOUNCE_BSDF,
        DEBUG_OUTPUT_BOUNCE_PDF,
        DEBUG_OUTPUT_BOUNCE_WEIGHT,
        DEBUG_BOUNCE_IS_TRANSMISSION,
        DEBUG_OUTPUT_HEMISPHERE_VIEW_SIDE,
        DEBUG_OUTPUT_COUNT,
    };

    enum Flags {
        FLAG_NONE = 1 << 0,
        FLAG_CULL_BACKFACE = 1 << 1,
        FLAG_ACCUMULATE = 1 << 2,
        FLAG_LUMINANCE_CLAMP = 1 << 3,
        FLAG_INDIRECT_ENVIRONMENT_ONLY = 1 << 4,
        FLAG_POINT_LIGHTS = 1 << 5,
        FLAG_SHADOW_RAYS = 1 << 6,
        FLAG_ALPHA_SHADOWS = 1 << 7,
        FLAG_ENVIRONMENT_MAP = 1 << 8,
        FLAG_ENVIRONMENT_MIS = 1 << 9,
        FLAG_MATERIAL_DIFFUSE_WHITE = 1 << 10,
        FLAG_MATERIAL_USE_GEOMETRIC_NORMALS = 1 << 11,
        FLAG_MATERIAL_MIS = 1 << 12,
        FLAG_SHOW_NAN = 1 << 13,
        FLAG_SHOW_INF = 1 << 14,
        FLAG_SHADING_NORMAL_ADAPTATION = 1 << 15,
    };

    struct Parameters {
        int width;
        int height;
        D3D12_GPU_VIRTUAL_ADDRESS acceleration_structure;
        D3D12_GPU_VIRTUAL_ADDRESS instances;
        D3D12_GPU_VIRTUAL_ADDRESS materials;
        int num_of_lights;
        D3D12_GPU_VIRTUAL_ADDRESS lights;
        glm::vec3 camera_pos;
        glm::mat4x4 clip_to_world;
        bool reset_history;
        int min_bounces;
        int max_bounces;
        int debug_output;
        uint32_t flags;
        int output_descriptor;
        glm::vec3 environment_color;
        float environment_intensity;
        uint32_t seed;
        int environment_cube_map;
        int environment_importance_map;
        float luminance_clamp;
        float min_russian_roulette_continue_prob;
        float max_russian_roulette_continue_prob;
    };

    static constexpr int MAX_BOUNCES = 5;
    
    void Create(ID3D12Device5* device, UploadBuffer* upload_buffer);
    void Run(ID3D12GraphicsCommandList4* command_list, CpuMappedLinearBuffer* allocator, const Parameters* parameters);
    int AccumulatedFrames();
    void Destroy();
    
    private:
    
    enum RootArguments {
        ROOT_PARAMETER_CONSTANT_BUFFER,
        ROOT_PARAMETER_ACCELERATION_STRUCTURE,
        ROOT_PARAMETER_INSTANCES,
        ROOT_PARAMETER_MATERIALS,
        ROOT_PARAMETER_LIGHTS,
        ROOT_PARAMETER_COUNT,
    };

    enum HitGroup {
        HIT_GROUP_BOUNCE,
        HIT_GROUP_SHADOW,
        HIT_GROUP_COUNT,
    };

    enum MissShader {
        MISS_SHADER_BOUNCE,
        MISS_SHADER_SHADOW,
        MISS_SHADER_COUNT,
    };
    
    ShaderTableCollection shader_tables;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
    Microsoft::WRL::ComPtr<ID3D12StateObject> state_object;
    Microsoft::WRL::ComPtr<ID3D12Resource> shader_tables_resource;
    
    int accumulated_frames = 0;

};