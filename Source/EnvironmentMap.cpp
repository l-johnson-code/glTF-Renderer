#include "EnvironmentMap.h"

#include <cassert>
#include <filesystem>

#include <directx/d3dx12_barriers.h>
#include <directx/d3dx12_core.h>
#include <directx/d3dx12_property_format_table.h>
#include <directx/d3dx12_root_signature.h>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>
#include <spdlog/spdlog.h>
#include <stb/stb_image.h>
#include <tinyexr/tinyexr.h>

#include "GpuResources.h"

// Note: This is not perceptual roughness.
float EnvironmentMap::MipToRoughness(int mip_level, int mip_count)
{
    float result = (float)mip_level / ((float)mip_count - 1);
    result *= result;
    return result;
}

void EnvironmentMap::Init(Gpu::Resources* resources)
{
    HRESULT result;

    this->resources = resources;

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
    result = resources->CreateRootSignature(&root_signature_desc, &this->root_signature, "Environment Root Signature");
    assert(result == S_OK);

    // Create the pipelines.
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc = {};
    pipeline_desc.pRootSignature = this->root_signature.Get();
    pipeline_desc.CS = Gpu::Resources::LoadShader("Shaders/ConvertEquirectangularToCubemap.cs.bin");
    result = resources->CreateComputePipelineState(&pipeline_desc, &this->generate_cubemap_pipeline_state, "Convert Equirectangular To Cubemap");
    assert(result == S_OK);
    Gpu::Resources::FreeShader(pipeline_desc.CS);

    pipeline_desc.CS = Gpu::Resources::LoadShader("Shaders/GenerateMipLevelArray.cs.bin");
	result = resources->CreateComputePipelineState(&pipeline_desc, &this->generate_cube_mip_pipeline_state, "Generate Mip Level Array");
	assert(result == S_OK);
    Gpu::Resources::FreeShader(pipeline_desc.CS);

    pipeline_desc.CS = Gpu::Resources::LoadShader("Shaders/GenerateEnvironmentImportanceMap.cs.bin");
    result = resources->CreateComputePipelineState(&pipeline_desc, &this->generate_importance_map_pipeline_state, "Generate Environment Importance Map");
    assert(result == S_OK);
    Gpu::Resources::FreeShader(pipeline_desc.CS);

    pipeline_desc.CS = Gpu::Resources::LoadShader("Shaders/GenerateEnvironmentImportanceMapLevel.cs.bin");
    result = resources->CreateComputePipelineState(&pipeline_desc, &this->generate_importance_map_level_pipeline_state, "Generate Environment Importance Map Level");
    assert(result == S_OK);
    Gpu::Resources::FreeShader(pipeline_desc.CS);

    pipeline_desc.CS = Gpu::Resources::LoadShader("Shaders/FilterEnvironmentCubeMap.cs.bin");
    result = resources->CreateComputePipelineState(&pipeline_desc, &this->filter_cube_map_pipeline_state, "Filter Environment Cube Map");
    assert(result == S_OK);
    Gpu::Resources::FreeShader(pipeline_desc.CS);
}

void EnvironmentMap::LoadEnvironmentMapImage(UploadBuffer* upload_buffer, const char* filepath, Map* map)
{
    std::filesystem::path path(filepath);
    if (path.extension() == ".exr") {
        LoadEnvironmentMapImageExr(upload_buffer, filepath, map);
    } else if (path.extension() == ".hdr") {
        LoadEnvironmentMapImageHdr(upload_buffer, filepath, map);
    }
}

void EnvironmentMap::CreateEnvironmentMap(CommandContext* context, Gpu::Texture* equirectangular_image, Map* map)
{
    HRESULT result = S_OK;

	// Create the destination cubemap.
    uint16_t cube_map_resolution = std::max(((int)equirectangular_image->Width() / 4) / 2, 1) + 1; // TODO: I dont think this is correct.
	Gpu::TextureDesc cubemap_desc = 
    {
        .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .width = cube_map_resolution,
        .height = cube_map_resolution,
        .flags = (Gpu::TextureFlags)(Gpu::TEXTURE_FLAG_SRV | Gpu::TEXTURE_FLAG_UAV | Gpu::TEXTURE_FLAG_CUBE),
        .initial_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        .name = "Environment Cube",
    };
	result = resources->CreateTexture(&cubemap_desc, &map->cube);
	assert(result == S_OK);

	// Create the ggx map.
	const int smallest_mip = 4;
	uint8_t ggx_mips = std::max((int)std::floorf(std::log2f(cube_map_resolution)) + 1 - smallest_mip, 1);
    Gpu::TextureDesc ggx_desc = {
        .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .width = cube_map_resolution,
        .height = cube_map_resolution,
        .mip_levels = ggx_mips,
        .flags = (Gpu::TextureFlags)(Gpu::TEXTURE_FLAG_SRV | Gpu::TEXTURE_FLAG_UAV | Gpu::TEXTURE_FLAG_CUBE),
        .initial_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        .name = "Environment GGX",
    };
	result = resources->CreateTexture(&ggx_desc, &map->ggx);
	assert(result == S_OK);

	// Create the diffuse map.
	const uint16_t diffuse_resolution = 256;
    Gpu::TextureDesc diffuse_desc = {
        .format = DXGI_FORMAT_R16G16B16A16_FLOAT,
        .width = diffuse_resolution,
        .height = diffuse_resolution,
        .mip_levels = 1,
        .flags = (Gpu::TextureFlags)(Gpu::TEXTURE_FLAG_SRV | Gpu::TEXTURE_FLAG_UAV | Gpu::TEXTURE_FLAG_CUBE),
        .initial_state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        .name = "Environment Diffuse",
    };
	result = resources->CreateTexture(&diffuse_desc, &map->diffuse);
	assert(result == S_OK);

    GenerateCubemap(context, equirectangular_image, &map->cube);
    GenerateGgxCube(context, &map->cube, &map->ggx);
    GenerateDiffuseCube(context, &map->cube, &map->diffuse);
}

void EnvironmentMap::DestroyEnvironmentMap(Map* map)
{
    resources->FreeTexture(&map->cube);
    resources->FreeTexture(&map->ggx);
    resources->FreeTexture(&map->diffuse);
    resources->FreeBuffer(&map->alias);
    resources->FreeTexture(&map->pdf);
}

void EnvironmentMap::LoadEnvironmentMapImageExr(UploadBuffer* upload_buffer, const char* filepath, Map* map)
{
	HRESULT result = S_OK;

	// Load the image.
	uint16_t x, y;
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
	
    Gpu::TextureDesc desc = {
        .format = format,
        .width = x,
        .height = y,
        .mip_levels = 1,
        .flags = Gpu::TEXTURE_FLAG_SRV,
        .name = "Environment Map",
    };
    result = resources->CreateTexture(&desc, &this->equirectangular_image);
	if (result != S_OK) {
        SPDLOG_ERROR("Failed to create texture.");
        FreeEXRHeader(&exr_header);
        FreeEXRImage(&exr_image);
        return;
    }

	int pixel_size = exr_header.channels[0].pixel_type == TINYEXR_PIXELTYPE_HALF ? 2 : 4;
	int destination_stride = D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetBitsPerUnit(format) / 8;
	uint32_t row_pitch = 0;
	std::byte* upload_ptr = (std::byte*)upload_buffer->QueueTextureUpload(format, x, y, 1, this->equirectangular_image.Resource(), 0, &row_pitch);
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

    std::vector<float> unpacked(x * y * 3);
    if (pixel_size == 2) {
        for (int i = 0; i < y; i++) {
            for (int j = 0; j < x; j++) {
                unpacked[(i * x + j) * 3] = glm::unpackHalf1x16(*(uint16_t*)(exr_image.images[red_channel] + (i * x + j) * pixel_size));
                unpacked[(i * x + j) * 3 + 1] = glm::unpackHalf1x16(*(uint16_t*)(exr_image.images[green_channel] + (i * x + j) * pixel_size));
                unpacked[(i * x + j) * 3 + 2] = glm::unpackHalf1x16(*(uint16_t*)(exr_image.images[blue_channel] + (i * x + j) * pixel_size));
            }
        }
    } else {
        for (int i = 0; i < y; i++) {
            for (int j = 0; j < x; j++) {
                unpacked[(i * x + j) * 3] = *(float*)(exr_image.images[red_channel] + (i * x + j) * pixel_size);
                unpacked[(i * x + j) * 3 + 1] = *(float*)(exr_image.images[green_channel] + (i * x + j) * pixel_size);
                unpacked[(i * x + j) * 3 + 2] = *(float*)(exr_image.images[blue_channel] + (i * x + j) * pixel_size);
            }
        }
    }
    GenerateAliasTable(upload_buffer, map, x, y, unpacked.data());

	FreeEXRImage(&exr_image);
	FreeEXRHeader(&exr_header);
}

void EnvironmentMap::LoadEnvironmentMapImageHdr(UploadBuffer* upload_buffer, const char* filepath, Map* map)
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

	Gpu::TextureDesc desc = {
        .format = format,
        .width = (uint16_t)x,
        .height = (uint16_t)y,
        .mip_levels = 1,
        .flags = Gpu::TEXTURE_FLAG_SRV,
        .name = "Environment Map",
    };
    result = resources->CreateTexture(&desc, &this->equirectangular_image);
	if (result != S_OK) {
        SPDLOG_ERROR("Failed to create texture.");
        stbi_image_free(image);
        return;
    }

    uint32_t row_pitch = 0;
	std::byte* upload_ptr = (std::byte*)upload_buffer->QueueTextureUpload(format, x, y, 1, this->equirectangular_image.Resource(), 0, &row_pitch);
    if (!upload_ptr) {
        SPDLOG_ERROR("Not enough space on the upload buffer to allocate image.");
        stbi_image_free(image);
        return;
    }

	for (int i = 0; i < y; i++) {
		memcpy(upload_ptr + i * row_pitch, image + i * x * channels, sizeof(float) * x * channels);
	}

    GenerateAliasTable(upload_buffer, map, x, y, image);

    stbi_image_free(image);
}

void EnvironmentMap::GenerateCubemap(CommandContext* context, Gpu::Texture* equirectangular_image, Gpu::Texture* cubemap)
{
    // Convert the equirectangular map to a cubemap.
    context->SetComputeRootSignature(this->root_signature.Get());
    context->SetPipelineState(this->generate_cubemap_pipeline_state.Get());
    struct {
        int environment;
        int cube;
    } constant_buffer;

    constant_buffer = {
        .environment = equirectangular_image->Srv(),
        .cube = cubemap->Uav(),
    };
    context->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&constant_buffer));
    uint32_t thread_groups_x = ((cubemap->Width() * 6) + 7) / 8;
    uint32_t thread_groups_y = (cubemap->Height() + 7) / 8;
    context->Dispatch(thread_groups_x, thread_groups_y, 1);

    // Generate the mips.
    context->SetPipelineState(this->generate_cube_mip_pipeline_state.Get());
    for (int i = 1; i < cubemap->MipLevels(); i++) {
        struct {
            int input_descriptor;
            int output_descriptor;
        } constant_buffer;

        int output_width = cubemap->Width() >> i;
        constant_buffer = {
            .input_descriptor = cubemap->Uav(i - 1),
            .output_descriptor = cubemap->Uav(i),
        };

        context->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&constant_buffer));
        uint32_t thread_groups_x = ((output_width * 6) + 7) / 8;
        uint32_t thread_groups_y = (output_width + 7) / 8;
        context->Dispatch(thread_groups_x, thread_groups_y, 1);

        context->PushUavBarrier(cubemap->Resource());
        context->SubmitBarriers();
    }

	context->PushTransitionBarrier(cubemap->Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	context->SubmitBarriers();
}

void EnvironmentMap::FilterCube(CommandContext* context, Gpu::Texture* cubemap, Bsdf bsdf, float mip_bias, int num_of_samples, Gpu::Texture* filtered_cube_map)
{
    int mip_count = filtered_cube_map->MipLevels();
    int resolution = filtered_cube_map->Width();
        
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
        .input = cubemap->Srv(),
        .num_of_samples = num_of_samples,
        .mip_bias = mip_bias,
        .bsdf = bsdf,
    };
    context->SetComputeRootSignature(this->root_signature.Get());
    context->SetPipelineState(this->filter_cube_map_pipeline_state.Get());
    for (int i = 0; i < mip_count; i++) {
        constant_buffer.output = filtered_cube_map->Uav(i);
        constant_buffer.roughness = MipToRoughness(i, mip_count);
        context->SetComputeRootConstantBufferView(0, context->CreateConstantBuffer(&constant_buffer));
        uint32_t thread_groups_x = (resolution * 6 + 7) / 8;
        uint32_t thread_groups_y = (resolution + 7) / 8;
        context->Dispatch(thread_groups_x, thread_groups_y, 1);
        resolution /= 2;
        // TODO: Are there missing UAV barriers here?
    }
    
    context->PushTransitionBarrier(filtered_cube_map->Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    context->SubmitBarriers();
}

void EnvironmentMap::GenerateGgxCube(CommandContext* context, Gpu::Texture* cubemap, Gpu::Texture* ggx_cube_map)
{
    FilterCube(context, cubemap, BSDF_GGX, 2, 256, ggx_cube_map);
}

void EnvironmentMap::GenerateDiffuseCube(CommandContext* context, Gpu::Texture* cubemap, Gpu::Texture* diffuse_cube_map)
{
    FilterCube(context, cubemap, BSDF_DIFFUSE, 3, 512, diffuse_cube_map);
}

static float Luminance(glm::vec3 color)
{
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

static glm::vec2 PixelToUv(glm::ivec2 pixel, glm::ivec2 size)
{
    return (glm::vec2(pixel) + 0.5f) / glm::vec2(size);
}

glm::vec2 UvToUnitSquare(glm::vec2 uv)
{
    return uv * glm::vec2(2, -2) + glm::vec2(-1, 1);
}

glm::vec3 SquareToSphere(glm::vec2 square)
{
    float d = 1.0f - (glm::abs(square.x) + glm::abs(square.y));
    float r = 1.0f - glm::abs(d);
    float phi = (r == 0.0f) ? 0.0f : (glm::pi<float>() / 4.0f) * ((glm::abs(square.y) - glm::abs(square.x)) / r + 1.0f);
    float f = r * glm::sqrt(2.0f - r * r);
    glm::vec3 sphere;
    sphere.x = f * glm::sign(square.x) * glm::cos(phi);
    sphere.y = f * glm::sign(square.y) * glm::sin(phi);
    sphere.z = glm::sign(d) * (1 - r * r);
    return sphere;
}

static glm::vec2 DirectionToEquirectangular(glm::vec3 direction)
{
    glm::vec2 equirectangular_coordinates = glm::vec2(glm::atan(direction.y, direction.x) / glm::two_pi<float>(), 1.0f - ((direction.z + 1.0f) / 2.0f));
    return equirectangular_coordinates;
}

static int WrapAddress(int value, int max)
{
    return value >= 0 ? value % max : max + (value % max);
}

static float SampleImageLuminance(int width, int height, glm::vec3* data, glm::vec2 uv)
{
    glm::vec3 rgb;
    glm::vec2 weight = glm::vec2(uv.x * width - 0.5f, uv.y * height - 0.5);
    int x = floor(weight.x);
    int y = floor(weight.y);
    weight.x -= (float)x;
    weight.y -= (float)y;

    int y_coords[2] = {std::clamp(y, 0, height), std::clamp(y + 1, 0, height - 1)};
    int x_coords[2] = { WrapAddress(x, width), WrapAddress(x + 1, width) };
    rgb = glm::mix(
        glm::mix(data[y_coords[0] * width + x_coords[0]], data[y_coords[0] * width + x_coords[1]], weight.x),
        glm::mix(data[y_coords[1] * width + x_coords[0]], data[y_coords[1] * width + x_coords[1]], weight.x),
        weight.y
    );
    return Luminance(rgb);
}

void EnvironmentMap::GenerateAliasTable(UploadBuffer* upload, Map* map, int width, int height, float* data)
{
    const int pdf_size = 1024;

    std::vector<float> pdf(pdf_size * pdf_size);

    // Convert to equal area map and calculate total as we do.
    float total = 0.0f;
    for (int i = 0; i < pdf_size; i++) {
        for (int j = 0; j < pdf_size; j++) {
            glm::vec2 uv = PixelToUv(glm::ivec2(j, i), glm::ivec2(pdf_size));
            glm::vec2 square = UvToUnitSquare(uv);
            glm::vec3 direction = SquareToSphere(square);
            glm::vec2 equirectangular = DirectionToEquirectangular(direction);
            float luminance = SampleImageLuminance(width, height, (glm::vec3*)data, equirectangular);
            pdf[i * pdf_size + j] = luminance;
            total += luminance;
        }
    }

    // Use MIS compensation.
    float average = total / (float)(pdf_size * pdf_size);
    total = 0.0f;
    int alias_table_size = 0;
    for (auto& p: pdf) {
        p = glm::max(p - average, 0.0f);
        if (p > 0.0f) {
            alias_table_size++;
        }
        total += p;
    }
    
    std::vector<AliasMap> alias_table;
    alias_table.reserve(alias_table_size);
    std::vector<int> smaller;
    std::vector<int> larger;

    // Normalize PDF and begin construction of the alias table.
    for (int i = 0; i < (pdf_size * pdf_size); i++) {
        if (pdf[i] > 0.0) {
            pdf[i] /= total;
            float p = pdf[i] * alias_table_size;
            alias_table.push_back({
                .prob = p,
                .pixel = glm::u16vec2(i % pdf_size, i / pdf_size),
                .alias = glm::u16vec2(i % pdf_size, i / pdf_size),
            });
            if (p < 1.0f) {
                smaller.push_back(alias_table.size() - 1);
            } else {
                larger.push_back(alias_table.size() - 1);
            }
        }
    }

    while (!smaller.empty() && !larger.empty()) {
        int smaller_i = smaller.back();
        smaller.pop_back();
        int larger_i = larger.back(); 
        larger.pop_back();
        alias_table[smaller_i].alias = alias_table[larger_i].pixel;
        alias_table[larger_i].prob = (alias_table[larger_i].prob + alias_table[smaller_i].prob) - 1.0f;
        if (alias_table[larger_i].prob < 1.0f) {
            smaller.push_back(larger_i);
        } else {
            larger.push_back(larger_i);
        }
    }
    while (!larger.empty()) {
        int larger_i = larger.back();
        larger.pop_back();
        alias_table[larger_i].prob = 1.0f;
    }
    while (!smaller.empty()) {
        int smaller_i = smaller.back();
        smaller.pop_back();
        alias_table[smaller_i].prob = 1.0f;
    }

    // Create the alias table resource.
    Gpu::BufferDesc alias_desc = {
        .size = alias_table_size * sizeof(AliasMap),
        .flags = Gpu::BUFFER_FLAG_GENERATE_DESCRIPTOR,
        .structured_byte_stride = sizeof(AliasMap),
        .name = "Alias Table",
    };
	HRESULT result = resources->CreateBuffer(&alias_desc, &map->alias);
	assert(result == S_OK);

    // Create the PDF texture.
	Gpu::TextureDesc pdf_desc = {
        .format = DXGI_FORMAT_R16_FLOAT,
        .width = pdf_size,
        .height = pdf_size,
        .mip_levels = 1,
        .flags = Gpu::TEXTURE_FLAG_SRV,
        .name = "PDF",
    };
    resources->CreateTexture(&pdf_desc, &map->pdf);
	assert(result == S_OK);

    // Upload to GPU.
    void* ptr = upload->QueueBufferUpload(alias_table_size * sizeof(AliasMap), map->alias.Resource(), 0);
    memcpy(ptr, alias_table.data(), alias_table_size * sizeof(AliasMap));

    uint32_t row_pitch = 0;
    ptr = upload->QueueTextureUpload(DXGI_FORMAT_R16_FLOAT, pdf_size, pdf_size, 1, map->pdf.Resource(), 0, &row_pitch);
    for (int i = 0; i < pdf_size; i++) {
        for (int j = 0; j < pdf_size; j++) {
            *((uint16_t*)((std::byte*)ptr + i * row_pitch) + j) = glm::packHalf1x16(pdf[i * pdf_size + j]); 
        }
    }
}
