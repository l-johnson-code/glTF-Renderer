#pragma once

inline int CalculateThreadGroups(int threads, int thread_group_size)
{
	return (threads + thread_group_size - 1) / thread_group_size;
}