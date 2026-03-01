#include "Memory.h"

#include "Profiling.h"

void* Allocate(size_t size)
{
	ProfileZoneScoped();
	void* ptr = malloc(size);
	if (ptr) {
		ProfileAlloc(ptr, size);
	}
	return ptr;
}

void Free(void* ptr)
{
	ProfileZoneScoped();
	ProfileFree(ptr);
	free(ptr);
}

#ifdef PROFILING_OVERLOAD_NEW_AND_DELETE
// Overwrite new and delete operators so that we can profile allocations.
void* operator new(size_t size)
{
    return Allocate(size);
}

void operator delete(void* ptr) noexcept
{
    Free(ptr);
}
#endif

void Copy(void* destination, void* source, size_t size)
{
	ProfileZoneScoped();
	memcpy(destination, source, size);
}

void Copy(void* destination, void* source, size_t element_size, uint32_t element_count, size_t source_stride)
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

int CalculateGroupedAllocationSize(Allocation* allocations, int allocation_count)
{
	int required_size = 0;
	for (int i = 0; i < allocation_count; i++) {
		required_size = Align(required_size, allocations[i].alignment);
		required_size += allocations[i].size;
	}
	return required_size;
}

int CalculateGroupedAllocationSizeAndOffsets(Allocation* allocations, int allocation_count, int* offsets)
{
	int required_size = 0;
	for (int i = 0; i < allocation_count; i++) {
		required_size = Align(required_size, allocations[i].alignment);
		offsets[i] = required_size;
		required_size += allocations[i].size;
	}
	return required_size;
}

void ApplyGroupedAllocationOffsets(int* offsets, int offset_count, void* base, void** out_pointers)
{
	for (int i = 0; i < offset_count; i++) {
		out_pointers[i] = (std::byte*)base + offsets[i];
	}
}