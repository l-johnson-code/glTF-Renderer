#include "File.h"

#include <string>

#include <Windows.h>

#include "Memory.h"
#include "Profiling.h"

namespace File {

static std::wstring WideStringFromUtf8(const char* utf8)
{
    int required_length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring wide(required_length, '\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), wide.size());
    return wide;
}

void* Load(const char* filename, uint64_t* size)
{
    ProfileZoneScoped();
    *size = 0;
    std::wstring utf16_filepath = WideStringFromUtf8(filename);
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

bool Save(const char* filename, void* data, uint64_t size)
{
    std::wstring utf16_filepath = WideStringFromUtf8(filename);
    HANDLE file = CreateFileW(utf16_filepath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD bytes_written = 0;
    BOOL result =  WriteFile(file, data, size, &bytes_written, nullptr);
    return (bool)result;
}

}