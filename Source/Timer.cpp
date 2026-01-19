#include "Timer.h"

#include <SDL3/SDL.h>

void Timer::Create()
{
    this->last_count = SDL_GetPerformanceCounter();
}

float Timer::Delta()
{
    uint64_t new_count = SDL_GetPerformanceCounter();
    uint64_t delta = new_count - this->last_count;
    this->last_count = new_count;
    float result = (float)(delta) / (float)(SDL_GetPerformanceFrequency());
    return result;
}