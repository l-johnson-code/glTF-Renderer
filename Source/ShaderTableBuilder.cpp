#include "ShaderTableBuilder.h"

#include "Memory.h"

static constexpr int SHADER_IDENTIFIER_SIZE = 32;

int ShaderRecordBuilder::CalculateRequiredSize()
{
    return Align(SHADER_IDENTIFIER_SIZE, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}

void ShaderRecordBuilder::Create(void* data)
{
    this->data = data;
}

void ShaderRecordBuilder::SetShader(void* shader_identifier) {
    assert(data);
    assert(shader_identifier);
    if (shader_identifier) {
        memcpy(data, shader_identifier, SHADER_IDENTIFIER_SIZE);
    } else {
        memset(data, 0, SHADER_IDENTIFIER_SIZE);
    }
}

int ShaderRecordBuilder::Size()
{
    return Align(SHADER_IDENTIFIER_SIZE, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}

int ShaderTableBuilder::CalculateRequiredSize(int record_count)
{
    return record_count * CalculateStride();
}

void ShaderTableBuilder::Create(void* data, int record_count)
{
    this->data = data;
    this->record_count = record_count;
    this->stride = CalculateStride();
}

void ShaderTableBuilder::SetShader(int record_index, void* shader_identifier)
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

int ShaderTableBuilder::Size()
{
    return this->record_count * this->stride;
}

int ShaderTableBuilder::RecordCount()
{
    return this->record_count;
}

int ShaderTableBuilder::Stride()
{
    return this->stride;
}

int ShaderTableBuilder::CalculateStride()
{
    return Align(SHADER_IDENTIFIER_SIZE, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
}
   
int ShaderTableCollectionBuilder::CalculateRequiredSize(int miss_count, int hit_group_count, int callable_count) {
    Allocation allocations[4] = {
        {ShaderRecordBuilder::CalculateRequiredSize(), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
        {ShaderTableBuilder::CalculateRequiredSize(miss_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
        {ShaderTableBuilder::CalculateRequiredSize(hit_group_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
        {ShaderTableBuilder::CalculateRequiredSize(callable_count), D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT},
    };
    return CalculateGroupedAllocationSize(allocations, 4);
}

void ShaderTableCollectionBuilder::Create(void* data, int miss_count, int hit_group_count, int callable_count)
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

ShaderTableCollection ShaderTableCollectionBuilder::GetShaderTableCollection(D3D12_GPU_VIRTUAL_ADDRESS base_address)
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