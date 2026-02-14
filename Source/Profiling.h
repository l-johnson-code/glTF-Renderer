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
    #ifdef PROFILING_CAPTURE_CALLSTACKS
    constexpr int callstack_depth = 62;
    #else
    constexpr int callstack_depth = 0;
    #endif
};

#if TRACY_ENABLE
#define ProfileZoneScoped() ZoneScopedS(Profiling::callstack_depth)
#define ProfileZoneScopedN(name) ZoneScopedNS(name, Profiling::callstack_depth)
#define ProfileMarkFrame() FrameMark
#define ProfilePlotBytes(name, bytes) TracyPlotConfig(name, tracy::PlotFormatType::Memory, true, true, 0); TracyPlot(name, bytes)
#define ProfilePlotNumber(name, number) TracyPlotConfig(name, tracy::PlotFormatType::Number, true, true, 0); TracyPlot(name, number)
#define ProfileAlloc(ptr, size) TracyAllocS(ptr, size, Profiling::callstack_depth)
#define ProfileFree(ptr) TracyFreeS(ptr, Profiling::callstack_depth)
#define ProfileAllocP(ptr, size, memory_pool) TracyAllocNS(ptr, size, Profiling::callstack_depth, Profiling::memory_pool_strings[memory_pool])
#define ProfileFreeP(ptr, memory_pool) TracyFreeNS(ptr, Profiling::callstack_depth, Profiling::memory_pool_strings[memory_pool])
#define ProfileFreeAllP(memory_pool) TracyMemoryDiscardS(Profiling::memory_pool_strings[memory_pool], Profiling::callstack_depth)
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