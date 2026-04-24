#pragma once

#include <string>

#include "Memory.h"

class Config {
	public:

	// Compile time configuration.
	static constexpr int DYNAMIC_DESCRIPTORS = 65536;
	static constexpr int PER_FRAME_DESCRIPTORS = 1024;
	static constexpr int MAX_SAMPLERS = 2048; // Maximum size for a shader visible sampler descriptor heap.
	static constexpr int FRAME_HEAP_CAPACITY = Mebibytes(512);
	static constexpr int FRAME_COUNT = 2;
    static constexpr int UPLOAD_BUFFER_CAPACITY = Mebibytes(512);
	static constexpr int MIN_WIDTH = 800;
	static constexpr int MIN_HEIGHT = 600;
	static constexpr int MAX_SIMULTANEOUS_MORPH_TARGETS = 4;
	static constexpr int MINIMUM_WINDOW_WIDTH = 800;
	static constexpr int MINIMUM_WINDOW_HEIGHT = 600;
    static constexpr int MAX_TLAS_INSTANCES = 1000;
    static constexpr uint32_t MAX_BLAS_VERTICES = 1000000;

	// Runtime configuration.
	static bool enable_d3d12_debug_layer;
	static bool enable_gpu_based_validation;
	static std::string load_gltf;
	static std::string load_environment;
	static bool fullscreen;
	static int width;
	static int height;

	static bool ParseBoolean(std::string_view argument, const char* name, bool* value);
	static bool ParseString(std::string_view argument, const char* name, std::string* value);
	static bool ParseInt(std::string_view argument, const char* name, int* value);
	static void ParseCommandLineArguments(const char*const* arguments, int argument_count);
};