#pragma once

#include <cstddef>
#include <functional>
#include <algorithm>

#include <directx/d3d12.h>
#include <glm/gtc/packing.hpp>
#include <tinygltf/tiny_gltf.h>

#include "Profiling.h"

namespace tinygltf {
namespace tools {

inline D3D12_TEXTURE_ADDRESS_MODE TextureAddressConversion(int wrap_mode)
{
	switch (wrap_mode) {
		case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		case TINYGLTF_TEXTURE_WRAP_REPEAT:
			return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		default:
			return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	}
}

inline D3D12_FILTER TextureFilterConversion(int min_filter, int mag_filter)
{
	int min = 
		min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST ||
		min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR ||
		min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ?
		D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
	int mag = mag_filter == TINYGLTF_TEXTURE_FILTER_NEAREST ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;  
	int mip =
		min_filter == TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST ||
		min_filter == TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST ?
		D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
	return D3D12_ENCODE_BASIC_FILTER(min, mag, mip, D3D12_FILTER_REDUCTION_TYPE_STANDARD);
}

inline int GetTypeSize(const tinygltf::Accessor* accessor)
{
    return tinygltf::GetComponentSizeInBytes(accessor->componentType) * tinygltf::GetNumComponentsInType(accessor->type);
}

inline int GetStride(const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    if (accessor->bufferView != -1) {
        const tinygltf::BufferView& buffer_view = model->bufferViews[accessor->bufferView];
        return buffer_view.byteStride == 0 ? GetTypeSize(accessor) : buffer_view.byteStride;
    } else {
        return 0;
    }
}

inline std::byte* GetBufferPtr(const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    if (accessor->bufferView != -1) {
        const tinygltf::BufferView& buffer_view = model->bufferViews[accessor->bufferView];
        const tinygltf::Buffer& buffer = model->buffers[buffer_view.buffer];
        return (std::byte*)&buffer.data[accessor->byteOffset + buffer_view.byteOffset];
    }
	return nullptr;
}

inline std::byte* GetSparseIndexPtr(const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    if (accessor->sparse.isSparse) {
        const tinygltf::BufferView& sparse_index_buffer_view = model->bufferViews[accessor->sparse.indices.bufferView];
        const tinygltf::Buffer& sparse_index_buffer = model->buffers[sparse_index_buffer_view.buffer];
        return (std::byte*)&sparse_index_buffer.data[accessor->sparse.indices.byteOffset + sparse_index_buffer_view.byteOffset];
    }
	return nullptr;
}

inline int GetSparseIndexStride(const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    if (accessor->sparse.isSparse) {
        const tinygltf::BufferView& sparse_index_buffer_view = model->bufferViews[accessor->sparse.indices.bufferView];
        return sparse_index_buffer_view.byteStride == 0 ? tinygltf::GetComponentSizeInBytes(accessor->sparse.indices.componentType) : sparse_index_buffer_view.byteStride;
    }
	return 0;
}

inline std::byte* GetSparseValuePtr(const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    if (accessor->sparse.isSparse) {
        const tinygltf::BufferView& sparse_values_buffer_view = model->bufferViews[accessor->sparse.values.bufferView];
        const tinygltf::Buffer& sparse_values_buffer = model->buffers[sparse_values_buffer_view.buffer];
        return (std::byte*)&sparse_values_buffer.data[accessor->sparse.values.byteOffset + sparse_values_buffer_view.byteOffset];
    }
	return nullptr;
}

inline int GetSparseValueStride(const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    if (accessor->sparse.isSparse) {
        const tinygltf::BufferView& sparse_values_buffer_view = model->bufferViews[accessor->sparse.values.bufferView];
        return sparse_values_buffer_view.byteStride == 0 ? GetTypeSize(accessor) : sparse_values_buffer_view.byteStride;
    }
	return 0;
}

inline uint32_t GetSparseIndex(std::byte* sparse_indices, int stride, int i, uint32_t component_type)
{
    std::byte* index = sparse_indices + stride * i;
    switch (component_type) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return *(uint8_t*)index;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return *(uint16_t*)index;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return *(uint32_t*)index;
        default: return 0;
    }
}

inline bool IsContiguous(const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    return GetStride(model, accessor) == GetTypeSize(accessor);
}

template<typename T>
inline bool IsSameType(uint32_t component_type)
{
    return
        (std::is_same_v<float, T> && component_type == TINYGLTF_COMPONENT_TYPE_FLOAT) ||
        (std::is_same_v<int, T> && component_type == TINYGLTF_COMPONENT_TYPE_INT) ||
        (std::is_same_v<uint32_t, T> && component_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) ||
        (std::is_same_v<int16_t, T> && component_type == TINYGLTF_COMPONENT_TYPE_SHORT) ||
        (std::is_same_v<uint16_t, T> && component_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) ||
        (std::is_same_v<int8_t, T> && component_type == TINYGLTF_COMPONENT_TYPE_BYTE) ||
        (std::is_same_v<uint8_t, T> && component_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE);
}

inline float UnpackNormalizedValue(const std::byte* data, uint32_t input_type)
{
    switch (input_type) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return glm::unpackUnorm1x8(*(uint8_t*)data);
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            return glm::unpackSnorm1x8(*(int8_t*)data);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return glm::unpackUnorm1x16(*(uint16_t*)data);
        case TINYGLTF_COMPONENT_TYPE_SHORT:
            return glm::unpackSnorm1x16(*(int16_t*)data);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return glm::unpackUnorm<float, 1, uint32_t, glm::defaultp>(glm::u32vec1(*(uint32_t*)data)).x;
        case TINYGLTF_COMPONENT_TYPE_INT:
            return glm::unpackSnorm<float, 1, int32_t, glm::defaultp>(glm::i32vec1(*(int32_t*)data)).x;
        default:
            return 0.0f;
    }
}

template<typename T>
inline T PackNormalizedValue(float input)
{
    if constexpr (std::is_same_v<float, T>) {
        return input;
    } else if constexpr (std::is_unsigned_v<T>) {
        return glm::packUnorm<T, 1, float>(glm::vec1(input)).x;
    } else if constexpr (std::is_signed_v<T>) {
        return glm::packSnorm<T, 1, float>(glm::vec1(input)).x;
    }
}

template<typename T>
inline T Convert(const std::byte* data, uint32_t input_type)
{
    switch (input_type) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return (T)(*(uint8_t*)data);
        case TINYGLTF_COMPONENT_TYPE_BYTE:
            return (T)(*(int8_t*)data);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return (T)(*(uint16_t*)data);
        case TINYGLTF_COMPONENT_TYPE_SHORT:
            return (T)(*(int16_t*)data);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return (T)(*(uint32_t*)data);
        case TINYGLTF_COMPONENT_TYPE_INT:
            return (T)(*(int32_t*)data);
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            return (T)(*(float*)data);
        default:
            return (T)0;
    }
}

template<typename T>
inline T Convert(const std::byte* data, bool normalized, uint32_t input_type)
{
    if (!data) {
        return (T)0;
    } else if (IsSameType<T>(input_type)) {
        return *(T*)data;
    } else if (normalized) {
        return PackNormalizedValue<T>(UnpackNormalizedValue(data, input_type));
    } else {
        return Convert<T>(data, input_type);
    }
}

template<glm::length_t L, typename T>
inline glm::vec<L, T> Convert(const std::byte* data, bool normalized, uint32_t input_type, uint32_t input_components)
{
    glm::vec<L, T> result;
    for (int i = 0; i < std::min((uint32_t)L, input_components); i++) {
        result[i] = Convert<T>(data + tinygltf::GetComponentSizeInBytes(input_type) * i, normalized, input_type);
    }
    for (int i = input_components; i < L; i++) {
        result[i] = (T)1;
    } 
    return result;
}

inline void IterateRaw(const tinygltf::Model* model, const tinygltf::Accessor* accessor, const std::function<void(int, std::byte*)>& lambda)
{
    int sparse_i = 0;
    int data_i = 0;

    std::byte* data = GetBufferPtr(model, accessor);
    int data_stride = GetStride(model, accessor);

    std::byte* sparse_indices = GetSparseIndexPtr(model, accessor);
    int sparse_indices_stride = GetSparseIndexStride(model, accessor);

    std::byte* sparse_values = GetSparseValuePtr(model, accessor);
    int sparse_values_stride = GetSparseValueStride(model, accessor);

    int sparse_index = accessor->sparse.isSparse ? GetSparseIndex(sparse_indices, sparse_indices_stride, sparse_i, accessor->sparse.indices.componentType) : 0;
    
    while (data_i < accessor->count) {
        if (accessor->sparse.isSparse && sparse_index == data_i) { // Get the sparse data.
            lambda(data_i, sparse_values + sparse_index * sparse_values_stride);
            sparse_i++;
            if (sparse_i < accessor->sparse.count) {
                sparse_index = GetSparseIndex(sparse_indices, sparse_indices_stride, sparse_i, accessor->sparse.indices.componentType);
            }
        } else { // Get the original data.
            lambda(data_i, data + data_i * data_stride);
        }
        data_i++;
    }
}

template<glm::length_t L, typename T>
inline void Iterate(const tinygltf::Model* model, const tinygltf::Accessor* accessor, const std::function<void(int, const glm::vec<L, T>&)>& lambda)
{
    int sparse_i = 0;
    int data_i = 0;

    std::byte* data = GetBufferPtr(model, accessor);
    int data_stride = GetStride(model, accessor);

    std::byte* sparse_indices = GetSparseIndexPtr(model, accessor);
    int sparse_indices_stride = GetSparseIndexStride(model, accessor);

    std::byte* sparse_values = GetSparseValuePtr(model, accessor);
    int sparse_values_stride = GetSparseValueStride(model, accessor);

    int sparse_index = accessor->sparse.isSparse ? GetSparseIndex(sparse_indices, sparse_indices_stride, sparse_i, accessor->sparse.indices.componentType) : 0;
    
    while (data_i < accessor->count) {
        std::byte* raw = nullptr;
        if (accessor->sparse.isSparse && sparse_index == data_i) { // Get the sparse data.
            raw = sparse_values + sparse_index * sparse_values_stride;
            sparse_i++;
            if (sparse_i < accessor->sparse.count) {
                sparse_index = GetSparseIndex(sparse_indices, sparse_indices_stride, sparse_i, accessor->sparse.indices.componentType);
            }
        } else { // Get the original data.
            raw = data + data_i * data_stride;
        }
        glm::vec<L,T> value = Convert<L, T>(raw, accessor->normalized, accessor->componentType, tinygltf::GetNumComponentsInType(accessor->type));
        lambda(data_i, value);
        data_i++;
    }
}

// Performs a raw copy with no conversion for data that is stored contiguously.
inline void CopyContiguous(const tinygltf::Model* model, const tinygltf::Accessor* accessor, void* output)
{
    assert(!accessor->sparse.isSparse);
    assert(GetStride(model, accessor) == GetTypeSize(accessor));
    std::memcpy(output, GetBufferPtr(model, accessor), accessor->count * GetTypeSize(accessor));
}

template<glm::length_t L, typename T>
inline void Copy(glm::vec<L, T>* output, const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    ProfileZoneScoped();
    // Check if conversion is required.
    bool same_dimension = tinygltf::GetNumComponentsInType(accessor->type) == L;
    bool same_component = IsSameType<T>(accessor->componentType);
    bool contiguous = GetStride(model, accessor) == GetTypeSize(accessor);
    if (same_dimension && same_component && contiguous && !accessor->sparse.isSparse) {
        CopyContiguous(model, accessor, output);
    } else {
        Iterate<L, T>(model, accessor, [&](int i, const glm::vec<L, T>& data) {
            output[i] = data;
        });
    }
}

// Copy without conversion.
inline void Copy(std::byte* out, const tinygltf::Model* model, const tinygltf::Accessor* accessor)
{
    ProfileZoneScoped();
    int element_size = GetTypeSize(accessor);
    if (IsContiguous(model, accessor)) {
        CopyContiguous(model, accessor, out);
    } else {
        IterateRaw(model, accessor, [&](int i, std::byte* data) {
            if (data) {
                std::memcpy(&out[i * element_size], data, element_size);
            } else {
                std::memset(&out[i * element_size], 0, element_size);
            }
        });
    }
}

inline void GetValue(const tinygltf::Value& value, const char* name, float* out)
{
    if (value.Has(name)) {
	    *out = value.Get(name).GetNumberAsDouble();
	}
}

template<glm::length_t L>
inline void GetValue(const tinygltf::Value& value, const char* name, glm::vec<L, float>* out)
{
    if (value.Has(name)) {
        const tinygltf::Value& vec_value = value.Get(name);
        if (vec_value.IsArray() && vec_value.ArrayLen() == L) {
            for (int i = 0; i < L; i++) {
	            (*out)[i] = vec_value.Get(i).GetNumberAsDouble();
            }
        }
	}
}

}
}