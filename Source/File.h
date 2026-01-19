#pragma once

class File {
    public:
    static void* Load(const char* filename, size_t* size);
    static void Free(void** ptr);
};