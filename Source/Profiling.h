#pragma once

#if TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

namespace Profiling {
    enum MemoryPool {
        MEMORY_POOL_CPU,
        MEMORY_POOL_GPU,
        MEMORY_POOL_COUNT,
    };
    extern const char* const memory_pool_strings[MEMORY_POOL_COUNT];
};

#if TRACY_ENABLE
#define ProfileZoneScoped() ZoneScoped
#define ProfileZoneScopedN(name) ZoneScopedN(name)
#define ProfileMarkFrame() FrameMark
#define ProfilePlotBytes(name, bytes) TracyPlotConfig(name, tracy::PlotFormatType::Memory, true, true, 0); TracyPlot(name, bytes)
#define ProfilePlotNumber(name, number) TracyPlotConfig(name, tracy::PlotFormatType::Number, true, true, 0); TracyPlot(name, number)
#define ProfileAlloc(ptr, size) TracyAlloc(ptr, size)
#define ProfileFree(ptr) TracyFree(ptr)
#define ProfileAllocP(ptr, size, memory_pool) TracyAllocN(ptr, size, Profiling::memory_pool_strings[memory_pool])
#define ProfileFreeP(ptr, memory_pool) TracyFreeN(ptr, Profiling::memory_pool_strings[memory_pool])
#define ProfileFreeAllP(memory_pool) TracyMemoryDiscard(ptr, Profiling::memory_pool_strings[memory_pool])
#else
#define ProfileZoneScoped()
#define ProfileZoneScopedN(name)
#define ProfileMarkFrame()
#define ProfilePlotBytes(name, bytes)
#define ProfilePlotNumber(name, number)
#define ProfileAlloc(ptr, size)
#define ProfileFree(ptr)
#define ProfileAllocP(ptr, size, memory_pool)
#define ProfileFreeP(ptr, memory_pool)
#define ProfileFreeAllP(memory_pool)
#endif