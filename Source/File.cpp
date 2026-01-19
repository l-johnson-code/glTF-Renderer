#include "File.h"

#include <cassert>
#include <string>

#include <Windows.h>

void* File::Load(const char* filename, size_t* size)
{
    int required_length = MultiByteToWideChar(CP_UTF8, 0, filename, -1, nullptr, 0);
    std::wstring utf16_filepath(required_length, '\0');
    MultiByteToWideChar(CP_UTF8, 0, filename, -1, utf16_filepath.data(), utf16_filepath.size());
    HANDLE file = CreateFileW(utf16_filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, NULL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        assert(false);
    }
	*size = GetFileSize(file, nullptr);
    void* data = malloc(*size);
    ReadFile(file, data, *size, nullptr, nullptr);
    CloseHandle(file);
    return data;
}

void File::Free(void** ptr)
{
    free(*ptr);
    *ptr = nullptr;
}