#pragma once

namespace File {
    void* Load(const char* filename, size_t* size);
    void Free(void* ptr);
};