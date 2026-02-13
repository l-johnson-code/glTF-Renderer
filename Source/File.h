#pragma once

#include <cstdint>

namespace File {
    void* Load(const char* filename, uint64_t* size);
    void Free(void* ptr);
};