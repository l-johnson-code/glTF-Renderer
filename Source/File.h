#pragma once

#include <cstdint>

namespace File {
    void* Load(const char* filename, uint64_t* size);
    void Free(void* ptr);
    bool Save(const char* filename, void* data, uint64_t size);
};