#include "File.h"

#include <cassert>
#include <string>

#include <Windows.h>

#include "Memory.h"
#include "Profiling.h"

namespace File {

void* Load(const char* filename, uint64_t* size)
{
    ProfileZoneScoped();
    *size = 0;
    int required_length = MultiByteToWideChar(CP_UTF8, 0, filename, -1, nullptr, 0);
    std::wstring utf16_filepath(required_length, '\0');
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, utf16_filepath.data(), utf16_filepath.size());
    HANDLE file = CreateFileW(utf16_filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, NULL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    LARGE_INTEGER file_size = {};
	BOOL size_result = GetFileSizeEx(file, &file_size);
    if (size_result == 0) {
        CloseHandle(file);
        return nullptr;
    }
    void* data = Allocate(file_size.QuadPart);
    if (!data) {
        CloseHandle(file);
        return nullptr;
    }
    BOOL read_result = ReadFile(file, data, file_size.QuadPart, nullptr, nullptr);
    if (read_result == 0) {
        ::Free(data);
        CloseHandle(file);
        return nullptr;
    }
    *size = file_size.QuadPart;
    return data;
}

void Free(void* ptr)
{
    ::Free(ptr);
}

}