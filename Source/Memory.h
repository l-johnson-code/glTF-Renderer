#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "Profiling.h"

inline void* Allocate(size_t size)
{
	ProfileZoneScoped();
	void* ptr = malloc(size);
	if (ptr) {
		ProfileAlloc(ptr, size);
	}
	return ptr;
}

inline void Free(void* ptr)
{
	ProfileZoneScoped();
	ProfileFree(ptr);
	free(ptr);
}

constexpr size_t Mebibytes(size_t mebibytes)
{
	return mebibytes << 20;
}

constexpr size_t Kibibytes(size_t kibibytes)
{
	return kibibytes << 10;
}

constexpr bool IsPowerOfTwo(size_t number)
{
	return (number != 0) && ((number & (number - 1)) == 0);
}

constexpr bool IsPowerOfTwoOrZero(size_t number)
{
	return (number & (number - 1)) == 0;
}

constexpr size_t AlignPowerOfTwo(size_t offset, size_t alignment)
{
	assert(IsPowerOfTwoOrZero(alignment));
	return alignment == 0 ? offset : (offset + alignment - 1) & (-alignment);
}

constexpr size_t Align(size_t offset, size_t alignment)
{
	return alignment == 0 ? offset : alignment * ((offset + alignment - 1) / alignment);
}

inline void Copy(void* destination, void* source, size_t size)
{
	ProfileZoneScoped();
	memcpy(destination, source, size);
}

inline void Copy(void* destination, void* source, size_t element_size, uint32_t element_count, size_t source_stride)
{
	ProfileZoneScoped();
	if (element_size == source_stride) {
		memcpy(destination, source, element_size * element_count);
	} else {
		for (uint32_t i = 0; i < element_count; i++) {
			std::byte* destination_bytes = (std::byte*)destination;
			std::byte* source_bytes = (std::byte*)source;
			memcpy(destination_bytes + i * element_size, source_bytes + i * source_stride, element_size);
		}
	}
}

struct Allocation {
	int size;
	int alignment;
};

inline int CalculateGroupedAllocationSize(Allocation* allocations, int allocation_count)
{
	int required_size = 0;
	for (int i = 0; i < allocation_count; i++) {
		required_size = Align(required_size, allocations[i].alignment);
		required_size += allocations[i].size;
	}
	return required_size;
}

inline int CalculateGroupedAllocationSizeAndOffsets(Allocation* allocations, int allocation_count, int* offsets)
{
	int required_size = 0;
	for (int i = 0; i < allocation_count; i++) {
		required_size = Align(required_size, allocations[i].alignment);
		offsets[i] = required_size;
		required_size += allocations[i].size;
	}
	return required_size;
}

inline void ApplyGroupedAllocationOffsets(int* offsets, int offset_count, void* base, void** out_pointers)
{
	for (int i = 0; i < offset_count; i++) {
		out_pointers[i] = (std::byte*)base + offsets[i];
	}
}