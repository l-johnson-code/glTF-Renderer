#pragma once

#include <cassert>
#include <cstdint>
#include <cstddef>

struct Allocation {
	int size;
	int alignment;
};

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

void* Allocate(size_t size);
void Free(void* ptr);
void Copy(void* destination, void* source, size_t size);
void Copy(void* destination, void* source, size_t element_size, uint32_t element_count, size_t source_stride);
int CalculateGroupedAllocationSize(Allocation* allocations, int allocation_count);
int CalculateGroupedAllocationSizeAndOffsets(Allocation* allocations, int allocation_count, int* offsets);
void ApplyGroupedAllocationOffsets(int* offsets, int offset_count, void* base, void** out_pointers);