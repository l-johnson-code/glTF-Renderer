#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <directx/d3d12.h>
#include <Windows.h>

#include "Profiling.h"

inline uint32_t CalculateThreadGroups(uint32_t threads, uint32_t thread_group_size)
{
	return (threads + thread_group_size - 1) / thread_group_size;
}

inline uint32_t MipSize(uint32_t size, uint16_t mip)
{
	return std::max(size >> mip, 1u);
}

inline uint32_t NextMipSize(uint32_t size)
{
	return std::max(size / 2u, 1u);
}

inline uint16_t MipCount(uint32_t width, uint32_t height)
{
	return std::floor(std::log2(std::max(width, height))) + 1u;
}

inline void SetName(ID3D12Object* object, const char* name)
{
	ProfileZoneScoped();
	// TODO: Use a variable length container like a vector to handle longer strings.
	thread_local wchar_t utf_16_name[1024];
	int result = MultiByteToWideChar(CP_UTF8, 0, name, -1, utf_16_name, std::size(utf_16_name));
	if (result != 0) {
		object->SetName(utf_16_name);
	}
}