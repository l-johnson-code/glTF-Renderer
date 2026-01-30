#include <filesystem>

#include <imgui/backends/imgui_impl_dx12.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#include "AnimationPlayer.h"
#include "Camera.h"
#include "Gltf.h"
#include "imgui.h"
#include "CameraController.h"
#include "Renderer.h"
#include "Timer.h"

struct Context {
	int scene_id = 0;
	int camera_id = -1;
	AnimationPlayer animation_player;
};

SDL_Window* g_window = nullptr;
bool g_running = true;
float g_window_scale = 1.0;
Renderer renderer;
Camera camera;
OrbitController g_orbit(glm::vec3(), 1., 0., 0.);
FreeController g_free(glm::vec3(0, -1, 0), 0, 0);
bool g_camera_free_mode = false;
Timer g_timer;
Gltf g_gltf;
Context g_context;

// Configuration.
Renderer::RenderSettings g_render_settings = {};

float GetWindowScaling(HWND window)
{
	return (float)GetDpiForWindow(window) / 96.; 
}

void LoadGltf(const char* filepath)
{
	g_context.animation_player = AnimationPlayer();
	renderer.WaitForOutstandingWork();
	renderer.upload_buffer.WaitForAllSubmissionsToComplete();
	g_gltf.Unload();
	renderer.upload_buffer.Begin();
	g_gltf.LoadFromGltf(filepath, renderer.device.Get(), &renderer.upload_buffer);
	g_context.scene_id = 0;
	renderer.upload_buffer.WaitForSubmissionToComplete(renderer.upload_buffer.Submit());
	g_render_settings.pathtracer.reset = true;
}

void Unload()
{
	g_context.animation_player = AnimationPlayer();
	renderer.WaitForOutstandingWork();
	renderer.upload_buffer.WaitForAllSubmissionsToComplete();
	g_gltf.Unload();
	g_context.scene_id = 0;
	g_render_settings.pathtracer.reset = true;
}

void LoadEnvironmentMap(const char* filepath)
{
	renderer.WaitForOutstandingWork();
	renderer.upload_buffer.Begin();
	renderer.environment_map.LoadEnvironmentMapImage(&renderer.upload_buffer, filepath);
	renderer.upload_buffer.WaitForSubmissionToComplete(renderer.upload_buffer.Submit());
	g_render_settings.pathtracer.reset = true;
}

bool BitflagCheckbox(const char* label, uint32_t* bits, uint32_t flag)
{
	bool temp_bool = *bits & flag;
	bool result = ImGui::Checkbox(label, &temp_bool);
	*bits = temp_bool ? *bits | flag : *bits & ~flag;
	return result;
}

bool BeginEnumWidget(const char* label, int* value, const char** strings, int num_of_strings, int* values = nullptr)
{
	int current_value = 0;
	if (values) {
		for (int i = 0; i < num_of_strings; i++) {
			if (values[i] == *value) {
				current_value = i;
				break;
			}
		}
	} else {
		current_value = *value;
	}
	return ImGui::BeginCombo(label, strings[current_value]);
}

bool AddEnumWidgetItem(int i, int* value, const char** strings, int* values = nullptr)
{
	bool value_changed = false;
	bool is_selected = values ? values[i] == *value : i == *value;
	if (ImGui::Selectable(strings[i], &is_selected)) {
		value_changed = values ? values[i] != *value : i != *value;
		*value = values ? values[i] : i;
	}
	return value_changed;
}

void EndEnumWidget()
{
	ImGui::EndCombo();
}

bool EnumWidget(const char* label, int* value, const char** strings, int num_of_strings, int* values = nullptr)
{
	bool value_changed = false;
	if (BeginEnumWidget(label, value, strings, num_of_strings, values)) {
		for (int i = 0; i < num_of_strings; i++) {
			value_changed |= AddEnumWidgetItem(i, value, strings, values);
		}
		EndEnumWidget();
	}
	return value_changed;
}

void ScheduleGltfLoad(const char* filepath)
{
	Config::load_gltf = filepath;
}

void ScheduleEnvironmentMapLoad(const char* filepath)
{
	Config::load_environment = filepath;
}

void OpenGltfFileDialog()
{
	SDL_DialogFileCallback callback = [](void* userdata, const char * const * filelist, int filter) {
		if (filelist && filelist[0]) {
			ScheduleGltfLoad(filelist[0]);
		}
	};
	SDL_DialogFileFilter filter = {"glTF", "gltf;glb"};
	SDL_ShowOpenFileDialog(callback, nullptr, nullptr, &filter, 1, nullptr, false);
}

void OpenEnvironmentFileDialog()
{
	SDL_DialogFileCallback callback = [](void* userdata, const char * const * filelist, int filter) {
		if (filelist && filelist[0]) {
			ScheduleEnvironmentMapLoad(filelist[0]);
		}
	};
	SDL_DialogFileFilter filter[] = {{"OpenEXR", "exr"}, {"Radiance RGBE", "hdr"}};
	SDL_ShowOpenFileDialog(callback, nullptr, nullptr, filter, 2, nullptr, false);
}

void DrawGltfTab(Gltf* gltf, Context* context)
{
	if (ImGui::Button("Load glTF")) {
		OpenGltfFileDialog();
	}
	if (ImGui::Button("Load Environment Map")) {
		OpenEnvironmentFileDialog();
	}

	// Camera.
    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::PushID("Camera");
		ImGui::Checkbox("Free Mode", &g_camera_free_mode);
        float vertical_fov_in_degrees = glm::degrees(camera.GetFov());
        ImGui::SliderFloat("FOV", &vertical_fov_in_degrees, 60., 120.);
        camera.SetFov(glm::radians(vertical_fov_in_degrees));
        ImGui::DragFloat("Near Plane", &camera.z_near, 1., 0., camera.z_near);
        ImGui::DragFloat("Far Plane", &camera.z_far, 1., camera.z_far);
        ImGui::PopID();
    }

	// Scenes.
    if (!gltf->scenes.empty()) {
        if (ImGui::BeginCombo("Scene", gltf->scenes[context->scene_id].name.c_str())) {
            for (int i = 0; i < gltf->scenes.size(); i++) {
                bool is_selected = i == context->scene_id;
                ImGui::PushID(i);
                if (ImGui::Selectable(gltf->scenes[i].name.c_str(), &is_selected)) {
                    context->scene_id = i;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }

    // Animations.
    if (!gltf->animations.empty()) {
        if (ImGui::BeginCombo("Animation", context->animation_player.animation == -1 ? "None" : gltf->animations[context->animation_player.animation].name.c_str())) {
            bool is_selected = context->animation_player.animation == -1;
            if (ImGui::Selectable("None", &is_selected)) {
                context->animation_player.animation = -1;
				g_render_settings.pathtracer.reset = true;
            }
            for (int i = 0; i < gltf->animations.size(); i++) {
                bool is_selected = i == context->animation_player.animation;
                ImGui::PushID(i);
                if (ImGui::Selectable(gltf->animations[i].name.c_str(), &is_selected)) {
                    context->animation_player.animation = i;
					g_render_settings.pathtracer.reset = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button(context->animation_player.playing ? "Pause" : "Play")) {
            context->animation_player.playing = !context->animation_player.playing;
        };
        ImGui::Checkbox("Loop", &context->animation_player.loop);
        if (context->animation_player.animation != -1) {
            g_render_settings.pathtracer.reset |= ImGui::SliderFloat("Animation Time", &context->animation_player.playhead, 0., gltf->animations[context->animation_player.animation].length);
        }
    }
}

void DrawGraphicsTab()
{
    // Tone mapping.
	if (ImGui::CollapsingHeader("Tonemapping")) {
		const char* tone_mapper_strings[] = {
			"None",
			"AgX",
		};
		EnumWidget("Tone Mapper", &g_render_settings.tone_mapper_config.tonemapper, tone_mapper_strings, std::size(tone_mapper_strings));
		ImGui::InputFloat("Exposure", &g_render_settings.tone_mapper_config.exposure);
	}

	// Display.
	if (ImGui::CollapsingHeader("Display")) {
		bool fullscreen = SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN ? true : false;
		if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
			SDL_SetWindowFullscreen(g_window, fullscreen);
		}
		bool v_sync = g_render_settings.vsync_interval == 1;
		if (ImGui::Checkbox("VSync", &v_sync)) {
			g_render_settings.vsync_interval = v_sync ? 1 : 0;
		}
	}

	if (ImGui::CollapsingHeader("Renderer")) {
		const char* renderer_type_strings[] = {
			"Rasterizer",
			"Pathtracer",
		};
		g_render_settings.pathtracer.reset |= EnumWidget("Renderer Type", (int*)&g_render_settings.renderer_type, renderer_type_strings, std::size(renderer_type_strings));

		if (g_render_settings.renderer_type == Renderer::RENDERER_TYPE_RASTERIZER) {
			ImGui::SliderInt("Transmission Downsample Sample Pattern", &g_render_settings.raster.transmission_downsample_sample_pattern, 0, ForwardPass::TRANSMISSION_DOWNSAMPLE_SAMPLE_PATTERN_COUNT - 1);
			ImGui::InputFloat("Bloom Strength", &g_render_settings.raster.bloom_strength);
			ImGui::SliderInt("Bloom Radius", &g_render_settings.raster.bloom_radius, 0, 6);
		}

		if (g_render_settings.renderer_type == Renderer::RENDERER_TYPE_PATHTRACER) {
			g_render_settings.pathtracer.reset |= ImGui::Button("Reset History");

			g_render_settings.pathtracer.reset |= ImGui::SliderInt("Maximum Bounces", &g_render_settings.pathtracer.max_bounces, 0, Pathtracer::MAX_BOUNCES);
			g_render_settings.pathtracer.reset |= ImGui::SliderInt("Minimum Bounces", &g_render_settings.pathtracer.min_bounces, 0, Pathtracer::MAX_BOUNCES);
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Min Russian Roulette Continue Probability", &g_render_settings.pathtracer.min_russian_roulette_continue_prob);
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Max Russian Roulette Continue Probability", &g_render_settings.pathtracer.max_russian_roulette_continue_prob);
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Max Ray Length", &g_render_settings.pathtracer.max_ray_length);

			const char* debug_output_strings[Pathtracer::DEBUG_OUTPUT_COUNT] = {
				"None",
				"Hit Kind",
				"Vertex Color",
				"Vertex Alpha",
				"Vertex Normal",
				"Vertex Tangent",
				"Vertex Bitangent",
				"Texcoord 0",
				"Texcoord 1",
				"Color",
				"Alpha",
				"Shading Normal",
				"Shading Tangent",
				"Shading Bitangent",
				"Metalness",
				"Roughness",
				"Specular",
				"Specular Color",
				"Clearcoat",
				"Clearcoat Roughness",
				"Clearcoat Normal",
				"Transmissive",
				"Bounce Direction",
				"Bounce BSDF",
    			"Bounce PDF",
    			"Bounce Weight",
    			"Bounce Is Transmission",
				"Hemisphere View Side"
			};
			g_render_settings.pathtracer.reset |= EnumWidget("Debug Output", &g_render_settings.pathtracer.debug_output, debug_output_strings, Pathtracer::DEBUG_OUTPUT_COUNT);

			g_render_settings.pathtracer.reset |= ImGui::Checkbox("Use Frame As Seed", &g_render_settings.pathtracer.use_frame_as_seed);
			ImGui::BeginDisabled(g_render_settings.pathtracer.use_frame_as_seed);
			g_render_settings.pathtracer.reset |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &g_render_settings.pathtracer.seed);
			ImGui::EndDisabled();

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Enable Point Lights", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_POINT_LIGHTS);
			g_render_settings.pathtracer.reset |= BitflagCheckbox("Shadow Rays", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_SHADOW_RAYS);
			ImGui::BeginDisabled(!(g_render_settings.pathtracer.flags & Pathtracer::FLAG_SHADOW_RAYS));
			g_render_settings.pathtracer.reset |= BitflagCheckbox("Alpha Shadows", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_ALPHA_SHADOWS);
			ImGui::EndDisabled();

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Indirect Environment Only", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_INDIRECT_ENVIRONMENT_ONLY);

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Cull Backface Triangles", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_CULL_BACKFACE);

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Accumulate", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_ACCUMULATE);
			ImGui::BeginDisabled(!(g_render_settings.pathtracer.flags & Pathtracer::FLAG_ACCUMULATE));
			ImGui::InputInt("Max Accumulated Frames", &g_render_settings.pathtracer.max_accumulated_frames);
			ImGui::EndDisabled();

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Enable Environment", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_ENVIRONMENT_MAP);
			g_render_settings.pathtracer.reset |= BitflagCheckbox("Importance Sample Environment Map", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_ENVIRONMENT_MIS);

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Enable Luminance Clamp", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_LUMINANCE_CLAMP);
			ImGui::BeginDisabled(!(g_render_settings.pathtracer.flags & Pathtracer::FLAG_LUMINANCE_CLAMP));
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Luminance Clamp", &g_render_settings.pathtracer.luminance_clamp);
			ImGui::EndDisabled();

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Material Diffuse White", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_MATERIAL_DIFFUSE_WHITE);
			g_render_settings.pathtracer.reset |= BitflagCheckbox("Use Geometric Normal", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_MATERIAL_USE_GEOMETRIC_NORMALS);
			g_render_settings.pathtracer.reset |= BitflagCheckbox("Use Multiple Importance Sampling", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_MATERIAL_MIS);

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Show NAN", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_SHOW_NAN);
			g_render_settings.pathtracer.reset |= BitflagCheckbox("Show INF", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_SHOW_INF);

			g_render_settings.pathtracer.reset |= BitflagCheckbox("Shading Normal Adaptation", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_SHADING_NORMAL_ADAPTATION);
		}
	}
}

void DrawUi()
{
	ImGui::SetNextWindowPos(ImVec2(0., 0.));
	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowSize(ImVec2(500 * g_window_scale, io.DisplaySize.y));
	ImGui::Begin("UI", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

	if (ImGui::BeginTabBar("Tabs"))
	{
		if (ImGui::BeginTabItem("glTF"))
		{
			DrawGltfTab(&g_gltf, &g_context);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Graphics"))
		{
			DrawGraphicsTab();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void ProcessEvents()
{
	SDL_Event event = {};
 	while (SDL_PollEvent(&event)) {
		bool event_handled = false;
		if (!ImGui::GetIO().WantCaptureMouse) {
			if (g_camera_free_mode) {
				event_handled = g_free.ProcessEvent(&event, g_window);
			} else {
				event_handled = g_orbit.ProcessEvent(&event);
			}
		}
		if (!event_handled) {
			event_handled = ImGui_ImplSDL3_ProcessEvent(&event);
		}
		if (!event_handled) {
			switch (event.type) {
				case SDL_EVENT_QUIT: {
					g_running = false;
				} break;
				case SDL_EVENT_WINDOW_RESIZED: {
					int new_width = event.window.data1;
					int new_height = event.window.data2;
					camera.SetAspectRatio((float)new_width / (float)new_height);
					g_render_settings.width = new_width;
					g_render_settings.height = new_height;
				} break;
				case SDL_EVENT_DROP_FILE: {
					std::filesystem::path filepath(event.drop.data);
					if (filepath.extension() == ".glb" || filepath.extension() == ".gltf") {
						ScheduleGltfLoad(event.drop.data);
					} else if (filepath.extension() == ".exr" || filepath.extension() == ".hdr") {
						ScheduleEnvironmentMapLoad(event.drop.data);
					}
				} break;
			}
		}
	}
}



int main(int argc, char* argv[])
{
	// Get command line arguments.
	Config::ParseCommandLineArguments(argv, argc);

	// Initialize SDL.
	bool sdl_result = true;
	sdl_result = SDL_SetAppMetadata("glTF Viewer", nullptr, nullptr);
	sdl_result = SDL_Init(0);

	// Create the window.
	SDL_PropertiesID window_properties = SDL_CreateProperties();
	SDL_SetStringProperty(window_properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "glTF Viewer");
	SDL_SetNumberProperty(window_properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, std::max(Config::width, Config::MINIMUM_WINDOW_WIDTH));
	SDL_SetNumberProperty(window_properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, std::max(Config::height, Config::MINIMUM_WINDOW_HEIGHT));
	SDL_SetBooleanProperty(window_properties, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, Config::fullscreen);
	SDL_SetBooleanProperty(window_properties, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
	g_window = SDL_CreateWindowWithProperties(window_properties);
	SDL_SetWindowMinimumSize(g_window, Config::MINIMUM_WINDOW_WIDTH, Config::MINIMUM_WINDOW_HEIGHT);

	// Window size is not guaranteed to be the size we requested.
	int window_width;
	int window_height;
	SDL_GetWindowSize(g_window, &window_width, &window_height);
	camera.Perspective((float)window_width / (float)window_height, glm::two_pi<float>() * 0.25, 0.01, 100.0);

	// Initialize ImGui.
	ImGui::CreateContext();
	ImGui_ImplSDL3_InitForD3D(g_window);
	g_window_scale = SDL_GetWindowDisplayScale(g_window);
	ImGui::GetStyle().ScaleAllSizes(g_window_scale);
	ImGuiIO& io = ImGui::GetIO();
	io.WantSaveIniSettings = false;
	float font_size = 14.0 * g_window_scale;
	io.Fonts->AddFontFromFileTTF("ProggyVector-Regular.ttf", font_size);

	// Initialise the renderer.
	int width, height;
	sdl_result = SDL_GetWindowSizeInPixels(g_window, &width, &height);
	SDL_PropertiesID properties_id = SDL_GetWindowProperties(g_window);
	if (properties_id == 0) {

	}
	HWND hwnd = (HWND)SDL_GetPointerProperty(properties_id, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
	
	g_render_settings.width = width;
	g_render_settings.height = height;
	g_render_settings.vsync_interval = 1;
	
	g_render_settings.renderer_type = Renderer::RENDERER_TYPE_RASTERIZER;
	g_render_settings.raster.render_flags = ForwardPass::RENDER_FLAG_POINT_LIGHTS | ForwardPass::RENDER_FLAG_ENVIRONMENT;

	g_render_settings.pathtracer.flags = 
        Pathtracer::FLAG_ACCUMULATE |
        Pathtracer::FLAG_POINT_LIGHTS |
        Pathtracer::FLAG_SHADOW_RAYS |
        Pathtracer::FLAG_ENVIRONMENT_MAP |
		Pathtracer::FLAG_ENVIRONMENT_MIS |
        Pathtracer::FLAG_MATERIAL_MIS |
		Pathtracer::FLAG_SHADING_NORMAL_ADAPTATION;
	g_render_settings.pathtracer.min_bounces = 2;
	g_render_settings.pathtracer.max_bounces = 2;
	g_render_settings.pathtracer.use_frame_as_seed = true;
	g_render_settings.pathtracer.luminance_clamp = 20.0;
	g_render_settings.pathtracer.max_accumulated_frames = 8196;

    renderer.Init(hwnd, &g_render_settings);

	g_gltf.Init(&renderer.resources.cbv_uav_srv_dynamic_allocator, &renderer.resources.gltf_sampler_allocator);

	g_timer.Create();

	// Main loop.
    while (g_running) {

        ProcessEvents();

		// Load a glTF model.
		if (!Config::load_gltf.empty()) {
			LoadGltf(Config::load_gltf.c_str());
			Config::load_gltf.clear();
			g_render_settings.pathtracer.reset = true;
		}

		// Load an environment.
		if (!Config::load_environment.empty()) {
			LoadEnvironmentMap(Config::load_environment.c_str());
			Config::load_environment.clear();
			g_render_settings.pathtracer.reset = true;
		}


		float delta_time = g_timer.Delta();

		if (g_camera_free_mode) {
			g_free.Tick(delta_time);
		}

		// Per frame stuff.
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		DrawUi();

		glm::mat4x4 camera_transform = g_camera_free_mode ? g_free.GetTransform() : g_orbit.GetTransform();
		camera.SetWorldToView(camera_transform);

		g_gltf.ApplyRestTransforms();

		// Animate.
		if (g_context.animation_player.playing) {
			g_render_settings.pathtracer.reset = true;
		}
		g_context.animation_player.Tick(&g_gltf, delta_time);
		g_gltf.CalculateGlobalTransforms(g_context.scene_id);
		ImGui::Render();
		renderer.DrawFrame(&g_gltf, g_context.scene_id, &camera, &g_render_settings);
		g_render_settings.pathtracer.reset = false;
    }

	// Wait for all outstanding GPU work to complete before releasing resources.
	renderer.WaitForOutstandingWork();
	renderer.upload_buffer.WaitForAllSubmissionsToComplete();
	g_gltf.Unload();

	// Release resources.
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

    return 0;
}
