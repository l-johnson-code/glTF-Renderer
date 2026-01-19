#include "Animation.h"

#include <cmath>
#include <cassert>

#include <glm/gtc/packing.hpp>
#include <glm/gtc/quaternion.hpp>

static float GetInterpolationFactor(float time, float lower_time, float upper_time)
{
    float diff = upper_time - lower_time;
    // Handle the case where lower is equal to upper.
    if (diff == 0.0f) {
        return 0.0f;
    }
    float result = (time - lower_time) / diff;
    assert(!std::isnan(result));
    return (time - lower_time) / diff;
}

static float CubicSpline(float previous_point, float previous_tangent, float next_point, float next_tangent, float delta_time, float interpolation_value) 
{
    float t = interpolation_value;
    float t2 = t * t;
    float t3 = t2 * t;

    return (2 * t3 - 3 * t2 + 1) * previous_point + delta_time * (t3 - 2 * t2 + t) * previous_tangent + (-2 * t3 + 3 * t2) * next_point + delta_time * (t3 - t2) * next_tangent;
}

int Animation::Channel::GetStartKeyframe(float time)
{
    time = glm::clamp(time, times[0], times.back());
    int result = 0;
    for (int i = 1; i < times.size() && times[i] <= time; i++) {
        result = i;
    }
    return result;
}

int Animation::Channel::FormatSize()
{
    switch (format) {
        case FORMAT_FLOAT:
            return 4;
        case FORMAT_UNORM_16:
        case FORMAT_SNORM_16:
            return 2;
        case FORMAT_UNORM_8:
        case FORMAT_SNORM_8:
            return 1;
    }
}

float Animation::Channel::UnpackData(int keyframe, int component)
{
    int format_size = FormatSize();
    std::byte* data = &transforms[keyframe * this->width * format_size + component * format_size];
    switch (format) {
        case FORMAT_FLOAT:
            return *(float*)data;
        case FORMAT_UNORM_16:
            return glm::unpackUnorm1x16(*(glm::uint16*)data);
        case FORMAT_SNORM_16:
            return glm::unpackSnorm1x16(*(glm::uint16*)data);
        case FORMAT_UNORM_8:
            return glm::unpackUnorm1x8(*(glm::uint8*)data);
        case FORMAT_SNORM_8:
            return glm::unpackSnorm1x8(*(glm::uint8*)data);
    }
}

void Animation::Channel::GetTransform(float time, float* out)
{
    time = glm::clamp(time, times[0], times.back());
    
    // Get the two keyframes we need to interpolate between.
    int k_start = 0;
    for (int i = 1; i < times.size() && times[i] <= time; i++) {
        k_start = i;
    }
    int k_end = k_start;
    if (k_end + 1 < times.size() && times[k_end] < time) {
        k_end++;
    }

    // Interpolate the keyframes.
    switch (interpolation_mode) {
        case INTERPOLATION_MODE_STEP: {
            for (int i = 0; i < this->width; i++) {
                out[i] = UnpackData(k_start, i);
            }
        } break;
        case INTERPOLATION_MODE_LINEAR: {
            float interpolation_factor = GetInterpolationFactor(time, times[k_start], times[k_end]);
            if (this->path == PATH_ROTATION) {
                glm::quat start(UnpackData(k_start, 0), UnpackData(k_start, 1), UnpackData(k_start, 2), UnpackData(k_start, 3));
                glm::quat end(UnpackData(k_end, 0), UnpackData(k_end, 1), UnpackData(k_end, 2), UnpackData(k_end, 3));
                *(glm::quat*)out = glm::slerp(start, end, interpolation_factor);
            } else {
                for (int i = 0; i < this->width; i++) {
                    float start_value = UnpackData(k_start, i);
                    float end_value = UnpackData(k_end, i);
                    out[i] = std::lerp(start_value, end_value, interpolation_factor);
                }
            }
        } break;
        case INTERPOLATION_MODE_CUBIC_SPLINE: {
            float interpolation_factor = GetInterpolationFactor(time, times[k_start], times[k_end]);
            for (int i = 0; i < this->width; i++) {
                float duration = times[k_end] - times[k_start];
                float start_value = UnpackData(k_start * 3, i); // TODO: I think this is wrong.
                float in_tangent = UnpackData(k_start * 3, i);
                float end_value = UnpackData(k_end * 3, i);
                float out_tangent = UnpackData(k_end * 3, i);
                out[i] = CubicSpline(start_value, in_tangent, end_value, out_tangent, duration, interpolation_factor);
            }
            if (this->path == PATH_ROTATION) {
                glm::quat* rotation = (glm::quat*)out;
                *rotation = glm::normalize(*rotation);
            }
        } break;
    }
}