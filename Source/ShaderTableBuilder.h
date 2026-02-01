#pragma once

#include <directx/d3d12.h>

struct ShaderTableCollection {
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE ray_generation_shader_record;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE miss_shader_table;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE hit_group_table;
    D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE callable_shader_table;
};

class ShaderRecordBuilder {
    
    public:

    static int CalculateRequiredSize();
    void Create(void* data);
    void SetShader(void* shader_identifier);
    int Size();

    private:

    void* data = nullptr;
};

class ShaderTableBuilder {
    
    public:

    static int CalculateRequiredSize(int record_count);
    void Create(void* data, int record_count);
    void SetShader(int record_index, void* shader_identifier);
    int Size();
    int RecordCount();
    int Stride();

    private:
    void* data = nullptr;
    int record_count = 0;
    int stride = 0;

    static int CalculateStride();
};

class ShaderTableCollectionBuilder {
    
    public:
    
    ShaderRecordBuilder ray_generation_record;
    ShaderTableBuilder miss_table;
    ShaderTableBuilder hit_group_table;
    ShaderTableBuilder callable_table;
    
    static int CalculateRequiredSize(int miss_count, int hit_group_count, int callable_count);
    void Create(void* data, int miss_count, int hit_group_count, int callable_count);
    ShaderTableCollection GetShaderTableCollection(D3D12_GPU_VIRTUAL_ADDRESS base_address);

    private:

    int miss_table_offset = 0;
    int hit_group_table_offset = 0;
    int callable_table_offset = 0;
};