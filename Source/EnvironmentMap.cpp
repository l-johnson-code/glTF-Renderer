#include "EnvironmentMap.h"

#include <cassert>
#include <filesystem>

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>
#include <directx/d3dx12_root_signature.h>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <tinyexr/tinyexr.h>

#include "DirectXHelpers.h"
#include "GpuResources.h"

// Note: This is not perceptual roughness.
float EnvironmentMap::MipToRoughness(int mip_level, int mip_count)
{
    float result = (float)mip_level / ((float)mip_count - 1);
    result *= result;
    return result;
}

void EnvironmentMap::Init(ID3D12Device* device, CbvSrvUavPool* descriptor_allocator)
{
    HRESULT result;

    this->device = device;
    this->descriptor_allocator = descriptor_allocator;

    // Create the root signature.
    CD3DX12_ROOT_PARAMETER root_parameter;
    root_parameter.InitAsConstantBufferView(0);
    CD3DX12_STATIC_SAMPLER_DESC sampler_descs[] = {
        // For sampling a cubemap.
        CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
        // For sampling an equirectangular image. This wraps horizontally but clamps at the bottom and top.
        CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP)
    };
    CD3DX12_ROOT_SIGNATURE_DESC root_signature_desc(1, &root_parameter, std::size(sampler_descs), sampler_descs, D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
    result = GpuResources::CreateRootSignature(device, &root_signature_desc, &this->root_signature, "Environment Root Signature");
    assert(result == S_OK);

    // Create the pipelines.
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
    pipeline_desc.pRootSignature = this->root_signature.Get();
    pipeline_desc.CS = GpuResources::LoadShader("Shaders/ConvertEquirectangularToCubemap.cs.bin");
    result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->generate_cubemap_pipeline_state));
    assert(result == S_OK);
    GpuResources::FreeShader(pipeline_desc.CS);

    pipeline_desc.CS = GpuResources::LoadShader("Shaders/GenerateMipLevelArray.cs.bin");
	result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->generate_cube_mip_pipeline_state));
	assert(result == S_OK);
    GpuResources::FreeShader(pipeline_desc.CS);

    pipeline_desc.CS = GpuResources::LoadShader("Shaders/GenerateEnvironmentImportanceMap.cs.bin");
    result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->generate_importance_map_pipeline_state));
    assert(result == S_OK);
    GpuResources::FreeShader(pipeline_desc.CS);

    pipeline_desc.CS = GpuResources::LoadShader("Shaders/GenerateEnvironmentImportanceMapLevel.cs.bin");
    result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->generate_importance_map_level_pipeline_state));
    assert(result == S_OK);
    GpuResources::FreeShader(pipeline_desc.CS);

    pipeline_desc.CS = GpuResources::LoadShader("Shaders/FilterEnvironmentCubeMap.cs.bin");
    result = device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&this->filter_cube_map_pipeline_state));
    assert(result == S_OK);
    GpuResources::FreeShader(pipeline_desc.CS);
}

void EnvironmentMap::LoadEnvironmentMapImage(UploadBuffer* upload_buffer, const char* filepath)
{
    std::filesystem::path path(filepath);
    if (path.extension() == ".exr") {
        LoadEnvironmentMapImageExr(upload_buffer, filepath);
    } else if (path.extension() == ".hdr") {
        LoadEnvironmentMapImageHdr(upload_buffer, filepath);
    }
}

void EnvironmentMap::CreateEnvironmentMap(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, CbvSrvUavStack* transient_descriptors, ID3D12Resource* equirectangular_image, Map* map)
{
    HRESULT result = S_OK;
    D3D12_RESOURCE_DESC equirectangular_desc = equirectangular_image->GetDesc();

    CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);

	// Create the destination cubemap.
    int cube_map_resolution = std::max(((int)equirectangular_desc.Width / 4) / 2, 1) + 1; // TODO: I dont think this is correct.
	CD3DX12_RESOURCE_DESC cubemap_resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, cube_map_resolution, cube_map_resolution, 6);
	cubemap_resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &cubemap_resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&map->cube));
	assert(result == S_OK);
	SetName(map->cube.Get(), "Environment Cube Map");

	// Create the importance map.
	int importance_map_resolution = 1024;
	CD3DX12_RESOURCE_DESC importance_map_resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, importance_map_resolution, importance_map_resolution);
	importance_map_resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &importance_map_resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&map->importance));
	assert(result == S_OK);
	SetName(map->importance.Get(), "Environment Importance Map");

	// Create the ggx map.
	const int smallest_mip = 4;
	int ggx_mips = std::max((int)std::floorf(std::log2f(cube_map_resolution)) + 1 - smallest_mip, 1);
	CD3DX12_RESOURCE_DESC ggx_resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, cube_map_resolution, cube_map_resolution, 6, ggx_mips);
	ggx_resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &ggx_resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&map->ggx));
	assert(result == S_OK);
	SetName(map->ggx.Get(), "Environment GGX Cube Map");

	// Create the diffuse map.
	const int diffuse_resolution = 256;
	CD3DX12_RESOURCE_DESC diffuse_resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, diffuse_resolution, diffuse_resolution, 6, 1);
	diffuse_resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &diffuse_resource_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&map->diffuse));
	assert(result == S_OK);
	SetName(map->diffuse.Get(), "Environment Diffuse Cube Map");

    CD3DX12_SHADER_RESOURCE_VIEW_DESC cube_desc = CD3DX12_SHADER_RESOURCE_VIEW_DESC::TexCube(DXGI_FORMAT_R16G16B16A16_FLOAT);
	map->cube_srv_descriptor = descriptor_allocator->AllocateAndCreateSrv(map->cube.Get(), &cube_desc);
	map->diffuse_srv_descriptor = descriptor_allocator->AllocateAndCreateSrv(map->diffuse.Get(), &cube_desc);
	map->ggx_srv_descriptor = descriptor_allocator->AllocateAndCreateSrv(map->ggx.Get(), &cube_desc);
	map->importance_srv_descriptor = descriptor_allocator->AllocateAndCreateSrv(map->importance.Get(), nullptr);

    GenerateCubemap(command_list, allocator, transient_descriptors, equirectangular_image, map->cube.Get());
    GenerateGgxCube(command_list, allocator, transient_descriptors, map->cube_srv_descriptor, map->ggx.Get());
    GenerateDiffuseCube(command_list, allocator, transient_descriptors, map->cube_srv_descriptor, map->diffuse.Get());
    GenerateImportanceMap(command_list, allocator, transient_descriptors, map->cube_srv_descriptor, map->importance.Get());
}

void EnvironmentMap::DestroyEnvironmentMap(Map* map)
{
    descriptor_allocator->Free(map->cube_srv_descriptor);
    map->cube_srv_descriptor = -1;
    descriptor_allocator->Free(map->ggx_srv_descriptor);
    map->ggx_srv_descriptor = -1;
    descriptor_allocator->Free(map->diffuse_srv_descriptor);
    map->diffuse_srv_descriptor = -1;
    descriptor_allocator->Free(map->importance_srv_descriptor);
    map->importance_srv_descriptor = -1;
    map->cube.Reset();
    map->ggx.Reset();
    map->diffuse.Reset();
    map->importance.Reset();
}

void EnvironmentMap::LoadEnvironmentMapImageExr(UploadBuffer* upload_buffer, const char* filepath)
{
	HRESULT result = S_OK;

	// Load the image.
	int x, y;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	const char* error;

	EXRVersion exr_version;
	int ret = ParseEXRVersionFromFile(&exr_version, filepath);
	if (ret != TINYEXR_SUCCESS) {
        SPDLOG_ERROR("Failed to load EXR image.");
        return;
    }
    if (exr_version.tiled != 0 || exr_version.multipart != 0 || exr_version.non_image != 0) {
        SPDLOG_ERROR("Unsupported EXR format.");
        return;
    }
    
	EXRHeader exr_header;
	InitEXRHeader(&exr_header);
	ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, filepath, &error);
	if (ret != TINYEXR_SUCCESS) {
        SPDLOG_ERROR("Failed to load EXR image.");
        FreeEXRHeader(&exr_header);
        return;
    }

	if (exr_header.channels[0].pixel_type == TINYEXR_PIXELTYPE_HALF) {
        format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	} else if (exr_header.channels[0].pixel_type == TINYEXR_PIXELTYPE_FLOAT) {
        format = DXGI_FORMAT_R32G32B32_FLOAT;
	} else {
        SPDLOG_ERROR("Unsupported EXR format.");
        FreeEXRHeader(&exr_header);
        return;
    }
    
	// Get the RGB channels.
	int red_channel = -1;
	int green_channel = -1;
	int blue_channel = -1;
	for (int i = 0; i < exr_header.num_channels; i++) {
        if (std::string_view("R").compare(exr_header.channels[i].name) == 0) {
            red_channel = i;
		} else if (std::string_view("G").compare(exr_header.channels[i].name) == 0) {
            green_channel = i;
		} else if (std::string_view("B").compare(exr_header.channels[i].name) == 0) {
            blue_channel = i;
		}
	}
	if (red_channel == -1 || green_channel == -1 || blue_channel == -1) {
        SPDLOG_ERROR("EXR missing channels.");
        FreeEXRHeader(&exr_header);
        return;
	}
    
	EXRImage exr_image;
	InitEXRImage(&exr_image);

	ret = LoadEXRImageFromFile(&exr_image, &exr_header, filepath, &error);
	if (ret != TINYEXR_SUCCESS) {
        SPDLOG_ERROR("Failed to load EXR image.");
        FreeEXRHeader(&exr_header);
        FreeEXRImage(&exr_image);
        return;
    }
	x = exr_image.width;
	y = exr_image.height;

	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(format, x, y, 1, 1);
	
	result = this->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(this->equirectangular_image.ReleaseAndGetAddressOf()));
	if (result != S_OK) {
        SPDLOG_ERROR("Failed to create texture.");
        FreeEXRHeader(&exr_header);
        FreeEXRImage(&exr_image);
        return;
    }
	SetName(this->equirectangular_image.Get(), "Environment Map");

	int pixel_size = exr_header.channels[0].pixel_type == TINYEXR_PIXELTYPE_HALF ? 2 : 4;
	int destination_stride = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	uint32_t row_pitch = 0;
	std::byte* upload_ptr = (std::byte*)upload_buffer->QueueTextureUpload(format, x, y, 1, this->equirectangular_image.Get(), 0, &row_pitch);
	if (!upload_ptr) {
        SPDLOG_ERROR("Not enough space on the upload buffer to allocate image.");
        FreeEXRHeader(&exr_header);
        FreeEXRImage(&exr_image);
        return;
    }
    
    for (int i = 0; i < y; i++) {
		for (int j = 0; j < x; j++) {
			memcpy(upload_ptr + i * row_pitch + j * destination_stride, exr_image.images[red_channel] + (i * x + j) * pixel_size, pixel_size);
			memcpy(upload_ptr + i * row_pitch + j * destination_stride + pixel_size, exr_image.images[green_channel] + (i * x + j) * pixel_size, pixel_size);
			memcpy(upload_ptr + i * row_pitch + j * destination_stride + pixel_size * 2, exr_image.images[blue_channel] + (i * x + j) * pixel_size, pixel_size);
		}
	}

	FreeEXRImage(&exr_image);
	FreeEXRHeader(&exr_header);
}

void EnvironmentMap::LoadEnvironmentMapImageHdr(UploadBuffer* upload_buffer, const char* filepath)
{
	HRESULT result = S_OK;

	// Load the image.
	int x, y;
    int channels;
	DXGI_FORMAT format = DXGI_FORMAT_R32G32B32_FLOAT;
    
    float* image = stbi_loadf(filepath, &x, &y, &channels, 3);
    if (!image) {
        SPDLOG_ERROR("Failed to load file: {}.", filepath);
        return;
    }

	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(format, x, y, 1, 1);
	
	result = this->device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(this->equirectangular_image.ReleaseAndGetAddressOf()));
	if (result != S_OK) {
        SPDLOG_ERROR("Failed to create texture.");
        stbi_image_free(image);
        return;
    }
	SetName(this->equirectangular_image.Get(), "Environment Map");

    uint32_t row_pitch = 0;
	std::byte* upload_ptr = (std::byte*)upload_buffer->QueueTextureUpload(format, x, y, 1, this->equirectangular_image.Get(), 0, &row_pitch);
    if (!upload_ptr) {
        SPDLOG_ERROR("Not enough space on the upload buffer to allocate image.");
        stbi_image_free(image);
        return;
    }

	for (int i = 0; i < y; i++) {
		memcpy(upload_ptr + i * row_pitch, image + i * x * channels, sizeof(float) * x * channels);
	}
}

void EnvironmentMap::GenerateCubemap(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, CbvSrvUavStack* transient_descriptors, ID3D12Resource* equirectangular_image, ID3D12Resource* cubemap)
{
    // Create descriptors.
    int input_descriptor = transient_descriptors->Allocate(1);
    transient_descriptors->CreateSrv(input_descriptor, equirectangular_image, nullptr);

    D3D12_RESOURCE_DESC cube_desc = cubemap->GetDesc();
    int mip_descriptor_start = transient_descriptors->Allocate(cube_desc.MipLevels);
    for (int i = 0; i < cube_desc.MipLevels; i++) {
		CD3DX12_UNORDERED_ACCESS_VIEW_DESC desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray(cube_desc.Format, 6, 0, i);
		transient_descriptors->CreateUav(mip_descriptor_start + i, cubemap, nullptr, &desc);
	}

    // Convert the equirectangular map to a cubemap.
    command_list->SetComputeRootSignature(this->root_signature.Get());
    command_list->SetPipelineState(this->generate_cubemap_pipeline_state.Get());
    struct {
        int environment;
        int cube;
    } constant_buffer;

    constant_buffer = {
        .environment = input_descriptor,
        .cube = mip_descriptor_start,
    };
    command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
    uint32_t thread_groups_x = ((cube_desc.Width * 6) + 7) / 8;
    uint32_t thread_groups_y = (cube_desc.Height + 7) / 8;
    command_list->Dispatch(thread_groups_x, thread_groups_y, 1);

    // Generate the mips.
    command_list->SetPipelineState(this->generate_cube_mip_pipeline_state.Get());
    for (int i = 1; i < cube_desc.MipLevels; i++) {
        struct {
            int input_descriptor;
            int output_descriptor;
        } constant_buffer;

        int output_width = cube_desc.Width >> i;
        constant_buffer = {
            .input_descriptor = mip_descriptor_start + i - 1,
            .output_descriptor = mip_descriptor_start + i,
        };

        command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
        uint32_t thread_groups_x = ((output_width * 6) + 7) / 8;
        uint32_t thread_groups_y = (output_width + 7) / 8;
        command_list->Dispatch(thread_groups_x, thread_groups_y, 1);

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(cubemap);
        command_list->ResourceBarrier(1, &barrier);
    }

	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(cubemap, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	command_list->ResourceBarrier(1, &barrier);
}

void EnvironmentMap::FilterCube(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, CbvSrvUavStack* transient_descriptors, int cubemap_srv_descriptor, Bsdf bsdf, float mip_bias, int num_of_samples, ID3D12Resource* filtered_cube_map)
{
    D3D12_RESOURCE_DESC ggx_cube_desc = filtered_cube_map->GetDesc();
    int mip_count = ggx_cube_desc.MipLevels;
    int resolution = ggx_cube_desc.Width;
    
	// Create descriptors for each mip.
	int mip_descriptors_start = transient_descriptors->Allocate(mip_count);
	for (int i = 0; i < mip_count; i++) {
        CD3DX12_UNORDERED_ACCESS_VIEW_DESC desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2DArray(DXGI_FORMAT_R16G16B16A16_FLOAT, 6, 0, i);
		transient_descriptors->CreateUav(mip_descriptors_start + i, filtered_cube_map, nullptr, &desc);
	}
    
    // Generate the mips.
    struct {
        int input;
        int output;
        float roughness;
        int num_of_samples;
        float mip_bias;
        int bsdf;
    } constant_buffer;
    constant_buffer = {
        .input = cubemap_srv_descriptor,
        .num_of_samples = num_of_samples,
        .mip_bias = mip_bias,
        .bsdf = bsdf,
    };
    command_list->SetComputeRootSignature(this->root_signature.Get());
    command_list->SetPipelineState(this->filter_cube_map_pipeline_state.Get());
    for (int i = 0; i < mip_count; i++) {
        constant_buffer.output = mip_descriptors_start + i;
        constant_buffer.roughness = MipToRoughness(i, mip_count);
        command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
        uint32_t thread_groups_x = (resolution * 6 + 7) / 8;
        uint32_t thread_groups_y = (resolution + 7) / 8;
        command_list->Dispatch(thread_groups_x, thread_groups_y, 1);
        resolution /= 2;
    }
    
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(filtered_cube_map, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    command_list->ResourceBarrier(1, &barrier);
}

void EnvironmentMap::GenerateGgxCube(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, CbvSrvUavStack* transient_descriptors, int cubemap_srv_descriptor, ID3D12Resource* ggx_cube_map)
{
    FilterCube(command_list, allocator, transient_descriptors, cubemap_srv_descriptor, BSDF_GGX, 2, 256, ggx_cube_map);
}

void EnvironmentMap::GenerateDiffuseCube(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, CbvSrvUavStack* transient_descriptors, int cubemap_srv_descriptor, ID3D12Resource* diffuse_cube_map)
{
    FilterCube(command_list, allocator, transient_descriptors, cubemap_srv_descriptor, BSDF_DIFFUSE, 3, 512, diffuse_cube_map);
}

void EnvironmentMap::GenerateImportanceMap(ID3D12GraphicsCommandList* command_list, CpuMappedLinearBuffer* allocator, CbvSrvUavStack* transient_descriptors, int cubemap_srv_descriptor, ID3D12Resource* importance_map)
{
    D3D12_RESOURCE_DESC importance_map_desc = importance_map->GetDesc();

	// Create descriptors for each mip.
    int mip_count = importance_map_desc.MipLevels;
	int mip_descriptors_start = transient_descriptors->Allocate(mip_count);
	for (int i = 0; i < mip_count; i++) {
		CD3DX12_UNORDERED_ACCESS_VIEW_DESC desc = CD3DX12_UNORDERED_ACCESS_VIEW_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, i);
		transient_descriptors->CreateUav(mip_descriptors_start + i, importance_map, nullptr, &desc);
	}

    // Generate first mip level.
    command_list->SetComputeRootSignature(this->root_signature.Get());
    command_list->SetPipelineState(this->generate_importance_map_pipeline_state.Get());
    struct {
        int environment_cube_map_srv;
        int environment_importance_map_uav;
    } constant_buffer;
    constant_buffer = {
        .environment_cube_map_srv = cubemap_srv_descriptor,
        .environment_importance_map_uav = mip_descriptors_start,
    };
    command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
    uint32_t thread_groups = (importance_map_desc.Width + 7) / 8;
    command_list->Dispatch(thread_groups, thread_groups, 1);
    
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(importance_map);
    command_list->ResourceBarrier(1, &barrier);

    // Generate all mip levels.
    command_list->SetPipelineState(this->generate_importance_map_level_pipeline_state.Get());
    for (int i = 1; i < importance_map_desc.MipLevels; i++) {
        int output_resolution = importance_map_desc.Width >> i;
        struct {
            int input_uav;
            int output_uav;
        } constant_buffer;
        constant_buffer = {
            .input_uav = mip_descriptors_start + i - 1,
            .output_uav = mip_descriptors_start + i,
        };
        command_list->SetComputeRootConstantBufferView(0, allocator->Copy(&constant_buffer, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
        uint32_t thread_groups = (output_resolution + 7) / 8;
        command_list->Dispatch(thread_groups, thread_groups, 1);

        CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(importance_map);
        command_list->ResourceBarrier(1, &barrier);
    }

    barrier = CD3DX12_RESOURCE_BARRIER::Transition(importance_map, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	command_list->ResourceBarrier(1, &barrier);
}
