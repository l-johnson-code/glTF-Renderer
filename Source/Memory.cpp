#include "Memory.h"

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