#include "Config.h"

#include <charconv>
#include <string_view>

bool Config::enable_d3d12_debug_layer = false;
bool Config::enable_gpu_based_validation = false;
std::string Config::load_gltf;
std::string Config::load_environment;
bool Config::fullscreen = false;
int Config::width = 1280;
int Config::height = 720;

bool Config::ParseBoolean(std::string_view argument, const char* name, bool* value)
{
    if (argument == name) {
        *value = true;
        return true;
    } else {
        return false;
    }
}

bool Config::ParseString(std::string_view argument, const char* name, std::string* value)
{
    if (argument.starts_with(name)) {
        *value = argument.substr(argument.find('=') + 1);
        return true;
    } else {
        return false;
    }
}

bool Config::ParseInt(std::string_view argument, const char* name, int* value)
{
    if (argument.starts_with(name)) {
        std::string_view string_value = argument.substr(argument.find('=') + 1);
        std::from_chars_result result = std::from_chars(string_value.data(), string_value.data() + string_value.size(), *value);
        return true;
    } else {
        return false;
    }
}

void Config::ParseCommandLineArguments(const char* const* arguments, int argument_count)
{
    for (int i = 1; i < argument_count; i++) {
        std::string_view argument(arguments[i]);
        if (ParseBoolean(argument, "--d3d12-debug-layer", &Config::enable_d3d12_debug_layer)) {
        } else if (ParseBoolean(argument, "--gpu-based-validation", &Config::enable_gpu_based_validation)) {
        } else if (ParseBoolean(argument, "--fullscreen", &Config::fullscreen)) {
        } else if (ParseString(argument, "--environment-map=", &load_environment)) {
        } else if (ParseInt(argument, "--width=", &width)) {
        } else if (ParseInt(argument, "--height=", &height)) {
        } else if (i == (argument_count - 1)) {
            load_gltf = std::string(argument);
        }
    }
}