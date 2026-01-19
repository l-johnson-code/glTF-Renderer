#pragma once

#include <vector>
#include <string>
#include <cstddef>

struct Animation 
{
    struct Channel {

        enum Path {
            PATH_TRANSLATION,
            PATH_ROTATION,
            PATH_SCALE,
            PATH_WEIGHTS,
        };

        enum InterpolationMode {
            INTERPOLATION_MODE_STEP,
            INTERPOLATION_MODE_LINEAR,
            INTERPOLATION_MODE_CUBIC_SPLINE,
        };

        enum Format {
            FORMAT_FLOAT,
            FORMAT_UNORM_8,
            FORMAT_UNORM_16,
            FORMAT_SNORM_8,
            FORMAT_SNORM_16,
        };

        int node_id;
        Format format;
        Path path;
        InterpolationMode interpolation_mode = INTERPOLATION_MODE_LINEAR;
        int width;
        std::vector<float> times;
        std::vector<std::byte> transforms;
        int FormatSize();
        float UnpackData(int keyframe, int component);
        int GetStartKeyframe(float time);
        void GetTransform(float time, float* out);
    };
    std::string name;
    float length = 0;
    std::vector<Channel> channels;
};