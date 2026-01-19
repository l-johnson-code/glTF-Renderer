#include "Pathtracer.h"

#include <algorithm>
#include <cassert>

#include <directx/d3dx12_core.h>
#include <directx/d3dx12_root_signature.h>

#include "GpuResources.h"

void Pathtracer::Create(ID3D12Device5* device, UploadBuffer* upload_buffer)
{
    HRESULT result = S_OK;

    CD3DX12_ROOT_PARAMETER root_parameters[ROOT_PARAMETER_COUNT];
    root_parameters[ROOT_PARAMETER_CONSTANT_BUFFER].InitAsConstantBufferView(0);
    root_parameters[ROOT_PARAMETER_ACCELERATION_STRUCTURE].InitAsShaderResourceView(0);
    root_parameters[ROOT_PARAMETER_INSTANCES].InitAsShaderResourceView(1);
    root_parameters[ROOT_PARAMETER_MATERIALS].InitAsShaderResourceView(2);
    root_parameters[ROOT_PARAMETER_LIGHTS].InitAsShaderResourceView(3);

    CD3DX12_STATIC_SAMPLER_DESC static_samplers[] = {
        CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP),
    };

    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(ROOT_PARAMETER_COUNT, root_parameters, std::size(static_samplers), static_samplers);
    root_signature_desc.Flags = 
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | 
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;
    
    Microsoft::WRL::ComPtr<ID3DBlob> root_signature_blob;
    Microsoft::WRL::ComPtr<ID3DBlob> root_signature_error_blob;
    result = D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &root_signature_blob, &root_signature_error_blob);
    assert(SUCCEEDED(result));
    result = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&this->root_signature));
    assert(SUCCEEDED(result));

    // Root signature shared between all shaders.
    D3D12_GLOBAL_ROOT_SIGNATURE global_root_signature = {
        .pGlobalRootSignature = this->root_signature.Get(),
    };

    // Shader code.
    D3D12_DXIL_LIBRARY_DESC dxil_library_desc = {
        .DXILLibrary = GpuResources::LoadShader("Shaders/PathTracer.lib.bin"),
        .NumExports = 0,
        .pExports = nullptr,
    };

    // Choose which functions form a hitgroup.
    D3D12_HIT_GROUP_DESC hit_group_desc = {
        .HitGroupExport = L"HitGroup",
        .Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
        .AnyHitShaderImport = L"AnyHit",
        .ClosestHitShaderImport = L"ClosestHit",
        .IntersectionShaderImport = nullptr,
    };

    D3D12_HIT_GROUP_DESC shadow_hit_group_desc = {
        .HitGroupExport = L"ShadowHitGroup",
        .Type = D3D12_HIT_GROUP_TYPE_TRIANGLES,
        .AnyHitShaderImport = L"ShadowAnyHit",
        .ClosestHitShaderImport = nullptr,
        .IntersectionShaderImport = nullptr,
    };

    D3D12_RAYTRACING_SHADER_CONFIG raytracing_shader_config = {
        .MaxPayloadSizeInBytes = sizeof(float) * 10,
        .MaxAttributeSizeInBytes = sizeof(float) * 2, // Size of BuiltInTriangleIntersectionAttributes.
    };

    D3D12_RAYTRACING_PIPELINE_CONFIG raytracing_pipeline_config = {
        .MaxTraceRecursionDepth = MAX_BOUNCES + 2,
    };

    D3D12_STATE_SUBOBJECT subobjects[] = {
        {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &global_root_signature},
        {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &dxil_library_desc},
        {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hit_group_desc},
        {D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &shadow_hit_group_desc},
        {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &raytracing_shader_config},
        {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &raytracing_pipeline_config},
    };

    D3D12_STATE_OBJECT_DESC state_object_desc = {
        .Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE,
        .NumSubobjects = std::size(subobjects),
        .pSubobjects = subobjects,
    };
    result = device->CreateStateObject(&state_object_desc, IID_PPV_ARGS(&this->state_object));
    assert(SUCCEEDED(result));

    // Create the shader table.
    int shader_table_size = ShaderTableCollectionBuilder::CalculateRequiredSize(1, 1, 0);
    CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(shader_table_size, D3D12_RESOURCE_FLAG_NONE, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&this->shader_tables_resource));
    assert(SUCCEEDED(result));
    void* shader_tables_data = upload_buffer->QueueBufferUpload(shader_table_size, this->shader_tables_resource.Get(), 0);
    assert(shader_tables_data);
    
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> state_object_properties;
    result = state_object.As(&state_object_properties);
    assert(SUCCEEDED(result));
    void* ray_generation_identifier = state_object_properties->GetShaderIdentifier(L"RayGeneration");
    void* hit_group_identifier = state_object_properties->GetShaderIdentifier(L"HitGroup");
    void* shadow_hit_group_identifier = state_object_properties->GetShaderIdentifier(L"ShadowHitGroup");
    void* miss_identifier = state_object_properties->GetShaderIdentifier(L"Miss");
    void* shadow_miss_identifier = state_object_properties->GetShaderIdentifier(L"ShadowMiss");

    ShaderTableCollectionBuilder collection_builder;
    collection_builder.Create(shader_tables_data, MISS_SHADER_COUNT, HIT_GROUP_COUNT, 0);
    collection_builder.ray_generation_record.SetShader(ray_generation_identifier);
    collection_builder.miss_table.SetShader(MISS_SHADER_BOUNCE, miss_identifier);
    collection_builder.miss_table.SetShader(MISS_SHADER_SHADOW, shadow_miss_identifier);
    collection_builder.hit_group_table.SetShader(HIT_GROUP_BOUNCE, hit_group_identifier);
    collection_builder.hit_group_table.SetShader(HIT_GROUP_SHADOW, shadow_hit_group_identifier);
    this->shader_tables = collection_builder.GetShaderTableCollection(this->shader_tables_resource->GetGPUVirtualAddress());

    // Cleanup.
    GpuResources::FreeShader(dxil_library_desc.DXILLibrary);
}

void Pathtracer::Run(ID3D12GraphicsCommandList4* command_list, CpuMappedLinearBuffer* allocator, const Parameters* parameters)
{
    if (parameters->reset_history) {
        this->accumulated_frames = 0;
    }

    struct {
        glm::mat4x4 clip_to_world;
        glm::vec3 camera_pos;
        int num_of_lights;
        int width;
        int height;
        uint32_t seed;
        int accumulated_frames;
        glm::vec3 environment_color;
        float environment_intensity;
        int debug_output;
        uint32_t flags;
        float max_ray_length;
        int min_bounces;
        int max_bounces;
        int output_descriptor;
        int environment_map_descriptor_id;
        int environment_importance_map_descriptor_id;
        float luminance_clamp;
        float min_russian_roulette_continue_prob;
        float max_russian_roulette_continue_prob;
    } constants;

    constants = {
        .clip_to_world = parameters->clip_to_world,
        .camera_pos = parameters->camera_pos,
        .num_of_lights = parameters->num_of_lights,
        .width = parameters->width,
        .height = parameters->height,
        .seed = parameters->seed,
        .accumulated_frames = this->accumulated_frames,
        .environment_color = parameters->environment_color,
        .environment_intensity = parameters->environment_intensity,
        .debug_output = parameters->debug_output,
        .flags = parameters->flags,
        .max_ray_length = 1000,
        .min_bounces = std::clamp(parameters->min_bounces, 0, MAX_BOUNCES),
        .max_bounces = std::clamp(parameters->max_bounces, 0, MAX_BOUNCES),
        .output_descriptor = parameters->output_descriptor,
        .environment_map_descriptor_id = parameters->environment_cube_map,
        .environment_importance_map_descriptor_id = parameters->environment_importance_map,
        .luminance_clamp = parameters->luminance_clamp,
        .min_russian_roulette_continue_prob = parameters->min_russian_roulette_continue_prob,
        .max_russian_roulette_continue_prob = parameters->max_russian_roulette_continue_prob,
    };

    D3D12_GPU_VIRTUAL_ADDRESS constant_buffer = allocator->Copy(&constants, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

    command_list->SetComputeRootSignature(this->root_signature.Get());
    command_list->SetComputeRootConstantBufferView(ROOT_PARAMETER_CONSTANT_BUFFER, constant_buffer);
    command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_ACCELERATION_STRUCTURE, parameters->acceleration_structure);
    command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_INSTANCES, parameters->instances);
    command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_MATERIALS, parameters->materials);
    command_list->SetComputeRootShaderResourceView(ROOT_PARAMETER_LIGHTS, parameters->lights);

    command_list->SetPipelineState1(this->state_object.Get());

    D3D12_DISPATCH_RAYS_DESC desc = {
        .RayGenerationShaderRecord = this->shader_tables.ray_generation_shader_record,
        .MissShaderTable = this->shader_tables.miss_shader_table,
        .HitGroupTable = this->shader_tables.hit_group_table,
        .CallableShaderTable = this->shader_tables.callable_shader_table,
        .Width = (uint32_t)parameters->width,
        .Height = (uint32_t)parameters->height,
        .Depth = 1,
    };
    command_list->DispatchRays(&desc);

    if (parameters->flags & FLAG_ACCUMULATE) {
        this->accumulated_frames++;
    } else {
        this->accumulated_frames = 0;
    }
}

int Pathtracer::AccumulatedFrames()
{
    return accumulated_frames;
}

void Pathtracer::Destroy()
{
    root_signature.Reset();
    state_object.Reset();
    shader_tables_resource.Reset();
}
