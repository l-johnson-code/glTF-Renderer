#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

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