#include <filesystem>

#include <imgui/backends/imgui_impl_dx12.h>
#include <imgui/backends/imgui_impl_sdl3.h>
#include <imgui/imgui_internal.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#include "AnimationPlayer.h"
#include "Camera.h"
#include "CameraController.h"
#include "DebugDraw.h"
#include "Gltf.h"
#include "ImGuiHelpers.h"
#include "Profiling.h"
#include "Renderer.h"
#include "Timer.h"

struct Context {
	int scene_id = 0;
	int camera_id = -1;
	int node_id = -1;
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

void Unload()
{
	// Wait for all uploads to complete before freeing the currently loaded scene.
	renderer.WaitForOutstandingWork();
	renderer.upload_buffer.WaitForAllSubmissionsToComplete();
	g_gltf.Unload();
	
	// Reset context.
	g_context.animation_player = AnimationPlayer();
	g_context.scene_id = 0;
	g_context.camera_id = -1;
	g_context.node_id = -1;

	// Invalidate pathtracer.
	g_render_settings.pathtracer.reset = true;
}

void LoadGltf(const char* filepath)
{
	Unload();
	renderer.upload_buffer.Begin();
	g_gltf.LoadFromGltf(filepath, &renderer.resources, &renderer.upload_buffer);
	renderer.upload_buffer.WaitForSubmissionToComplete(renderer.upload_buffer.Submit());
}

void LoadEnvironmentMap(const char* filepath)
{
	renderer.WaitForOutstandingWork();
	renderer.upload_buffer.Begin();
	renderer.environment_map.LoadEnvironmentMapImage(&renderer.upload_buffer, filepath, &renderer.map);
	renderer.upload_buffer.WaitForSubmissionToComplete(renderer.upload_buffer.Submit());
	g_render_settings.pathtracer.reset = true;
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

void DrawNode(Gltf* gltf, Context* context, int node_id)
{
	ImGui::PushID(node_id);
	ImGuiTreeNodeFlags flags = gltf->nodes[node_id].child != -1 ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf;
	if (context->node_id == node_id) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	bool is_open = ImGui::TreeNodeEx("", flags | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen, "[%i] %s", node_id, gltf->nodes[node_id].name.c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
		context->node_id = node_id;
	}
	if (is_open) {
		// Draw nodes children.
		for (int i = gltf->nodes[node_id].child; i != -1; i = gltf->nodes[i].sibling) {
			DrawNode(gltf, context, i);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void DrawMainMenuBar()
{
	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Load glTF")) {
				OpenGltfFileDialog();
			}
			if (ImGui::MenuItem("Load Environment Map")) {
				OpenEnvironmentFileDialog();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}
}

void DrawScenePanel(Gltf* gltf, Context* context)
{
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

	// Camera.
    if (ImGui::BeginSection("Camera")) {
		ImGui::Checkbox("Free Mode", &g_camera_free_mode);
        float vertical_fov_in_degrees = glm::degrees(camera.GetFov());
        ImGui::SliderFloat("FOV", &vertical_fov_in_degrees, 60., 120.);
        camera.SetFov(glm::radians(vertical_fov_in_degrees));
        ImGui::DragFloat("Near Plane", &camera.z_near, 1., 0., camera.z_near);
        ImGui::DragFloat("Far Plane", &camera.z_far, 1., camera.z_far);
		ImGui::EndSection();
    }

    // Animations.
    if (!gltf->animations.empty() && ImGui::BeginSection("Animation")) {
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
		ImGui::EndSection();
    }
}

void DrawPropertiesPanel(Gltf* gltf, Context* context)
{
	if (context->node_id != -1) {
		Gltf::Node& node = gltf->nodes[context->node_id];
		ImGui::LabelText("Name", "%s", node.name.c_str());
		ImGui::Input("Position", &node.local_transform.translation, ImGuiInputTextFlags_ReadOnly);
		ImGui::Input("Rotation", &node.local_transform.rotation, ImGuiInputTextFlags_ReadOnly);
		ImGui::Input("Scale", &node.local_transform.scale, ImGuiInputTextFlags_ReadOnly);
		if (node.camera_id != -1 && ImGui::BeginSection("Camera")) {
			ImGui::InputInt("Index", &node.camera_id, 0, 0, ImGuiInputTextFlags_ReadOnly);
			Camera& camera = gltf->cameras[node.camera_id];
			const char* camera_type_strings[] = {
				"Perspective",
				"Orthographic",
			};
			ImGui::LabelText("Type", "%s", camera_type_strings[camera.GetType()]);
			if (camera.GetType() == Camera::CAMERA_TYPE_PERSPECTIVE) {
				ImGui::InputFloat("FOV", &camera, &Camera::GetFov, &Camera::SetFov, ImGuiInputTextFlags_ReadOnly);
				ImGui::InputFloat("Aspect Ratio", &camera, &Camera::GetAspectRatio, &Camera::SetAspectRatio, ImGuiInputTextFlags_ReadOnly);
			} else {
				ImGui::InputFloat("X Mag", &camera, &Camera::GetXMag, &Camera::SetXMag, ImGuiInputTextFlags_ReadOnly);
				ImGui::InputFloat("Y Mag", &camera, &Camera::GetYMag, &Camera::SetYMag, ImGuiInputTextFlags_ReadOnly);
			}
			ImGui::InputFloat("Near Plane", &camera.z_near, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::InputFloat("Far Plane", &camera.z_far, 0.0f, 0.0f, "%.3f", ImGuiInputTextFlags_ReadOnly);
			ImGui::EndSection();
		}
		if (node.light_id != -1 && ImGui::BeginSection("Light")) {
			Gltf::Light& light = gltf->lights[node.light_id];
			ImGui::InputInt("Index", &node.light_id, 0, 0, ImGuiInputTextFlags_ReadOnly);
			const char* light_type_strings[] = {
				"Point",
				"Spot",
				"Directional",
			};
			ImGui::Enum("Type", &light.type, light_type_strings);
			ImGui::ColorEdit("Color", &light.color);
			ImGui::InputFloat("Intensity", &light.intensity);
			ImGui::InputFloat("Cutoff", &light.cutoff);
			if (light.type == Gltf::Light::TYPE_SPOT) {
				ImGui::InputFloat("Inner Angle", &light.inner_angle);
				ImGui::InputFloat("Outer Angle", &light.outer_angle);
			}
			ImGui::EndSection();
		}
		if (node.mesh_id != -1 && ImGui::BeginSection("Mesh")) {
			Gltf::Mesh& mesh = gltf->meshes[node.mesh_id];
			ImGui::LabelText("Name", "%s", mesh.name.c_str());
			for (int i = 0; i < mesh.primitives.size(); i++) {
				ImGui::PushID(i);
				Gltf::Primitive& primitive = mesh.primitives[i];
				ImGui::PopID();
			}
			ImGui::EndSection();
		}
		if (node.skin_id != -1 && ImGui::BeginSection("Skin")) {
			Gltf::Skin& skin = gltf->skins[node.skin_id];
			ImGui::InputInt("Skin ID", &node.skin_id, 0, 0, ImGuiInputTextFlags_ReadOnly);
			ImGui::LabelText("Skin Root", "[%i] %s", skin.joints[0], gltf->nodes[skin.joints[0]].name.c_str());
			ImGui::EndSection();
		}
	}
}

void DrawNodesPanel(Gltf* gltf, Context* context)
{
	if (!gltf->scenes.empty()) {
		for (int node_id : gltf->scenes[context->scene_id].nodes) {
			DrawNode(gltf, context, node_id);
		}
	}
}

void DrawGraphicsPanel()
{
    // Tone mapping.
	if (ImGui::BeginSection("Tonemapping")) {
		const char* tone_mapper_strings[] = {
			"None",
			"AgX",
		};
		ImGui::Enum("Tone Mapper", &g_render_settings.tone_mapper_config.tonemapper, tone_mapper_strings);
		ImGui::InputFloat("Exposure", &g_render_settings.tone_mapper_config.exposure);
		ImGui::EndSection();
	}

	// Display.
	if (ImGui::BeginSection("Display")) {
		bool fullscreen = SDL_GetWindowFlags(g_window) & SDL_WINDOW_FULLSCREEN ? true : false;
		if (ImGui::Checkbox("Fullscreen", &fullscreen)) {
			SDL_SetWindowFullscreen(g_window, fullscreen);
		}
		bool v_sync = g_render_settings.vsync_interval == 1;
		if (ImGui::Checkbox("VSync", &v_sync)) {
			g_render_settings.vsync_interval = v_sync ? 1 : 0;
		}
		ImGui::EndSection();
	}

	if (ImGui::BeginSection("Renderer")) {
		const char* renderer_type_strings[] = {
			"Rasterizer",
			"Pathtracer",
		};
		g_render_settings.pathtracer.reset |= ImGui::Enum("Renderer Type", &g_render_settings.renderer_type, renderer_type_strings);

		if (g_render_settings.renderer_type == Renderer::RENDERER_TYPE_RASTERIZER) {
			ImGui::Checkbox("Frustum Culling", &g_render_settings.raster.frustum_culling);
			ImGui::Checkbox("Draw Bounding Boxes", &g_render_settings.raster.draw_bounding_boxes);
			ImGui::SliderInt("Transmission Downsample Sample Pattern", &g_render_settings.raster.transmission_downsample_sample_pattern, 0, ForwardPass::TRANSMISSION_DOWNSAMPLE_SAMPLE_PATTERN_COUNT - 1);
			ImGui::InputFloat("Bloom Strength", &g_render_settings.raster.bloom_strength);
			ImGui::SliderInt("Bloom Radius", &g_render_settings.raster.bloom_radius, 0, 6);
		}

		if (g_render_settings.renderer_type == Renderer::RENDERER_TYPE_PATHTRACER) {
			g_render_settings.pathtracer.reset |= ImGui::Button("Reset History");

			g_render_settings.pathtracer.reset |= ImGui::SliderInt("Ray Rate", &g_render_settings.pathtracer.ray_rate, 1, 8);
			g_render_settings.pathtracer.reset |= ImGui::SliderInt("Maximum Bounces", &g_render_settings.pathtracer.max_bounces, 0, Pathtracer::MAX_BOUNCES);
			g_render_settings.pathtracer.reset |= ImGui::SliderInt("Minimum Bounces", &g_render_settings.pathtracer.min_bounces, 0, Pathtracer::MAX_BOUNCES);
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Min Russian Roulette Continue Probability", &g_render_settings.pathtracer.min_russian_roulette_continue_prob);
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Max Russian Roulette Continue Probability", &g_render_settings.pathtracer.max_russian_roulette_continue_prob);
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Russian Roulette Active Lane Threshold", &g_render_settings.pathtracer.russian_roulette_active_lane_threshold);
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Max Ray Length", &g_render_settings.pathtracer.max_ray_length);

			const char* debug_output_strings[Pathtracer::DEBUG_OUTPUT_COUNT] = {
				"None",
				"Hit Kind",
				"Vertex Color",
				"Vertex Alpha",
				"Vertex Normal",
				"Vertex Tangent",
				"Vertex Bitangent",
				"Texcoord",
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
				"Hemisphere View Side",
				"Environment Map Direction",
        		"Environment Map Color",
        		"Environment Map PDF",
			};
			g_render_settings.pathtracer.reset |= ImGui::Enum("Debug Output", &g_render_settings.pathtracer.debug_output, debug_output_strings);

			g_render_settings.pathtracer.reset |= ImGui::Checkbox("Use Frame As Seed", &g_render_settings.pathtracer.use_frame_as_seed);
			ImGui::BeginDisabled(g_render_settings.pathtracer.use_frame_as_seed);
			g_render_settings.pathtracer.reset |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &g_render_settings.pathtracer.seed);
			ImGui::EndDisabled();
			
			g_render_settings.pathtracer.reset |= ImGui::Checkbox("Jitter Matrix", &g_render_settings.pathtracer.jitter_matrix);

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Enable Point Lights", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_POINT_LIGHTS);
			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Shadow Rays", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_SHADOW_RAYS);
			ImGui::BeginDisabled(!(g_render_settings.pathtracer.flags & Pathtracer::FLAG_SHADOW_RAYS));
			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Alpha Shadows", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_ALPHA_SHADOWS);
			ImGui::EndDisabled();

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Indirect Environment Only", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_INDIRECT_ENVIRONMENT_ONLY);

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Cull Backface Triangles", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_CULL_BACKFACE);

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Accumulate", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_ACCUMULATE);
			ImGui::BeginDisabled(!(g_render_settings.pathtracer.flags & Pathtracer::FLAG_ACCUMULATE));
			ImGui::InputInt("Max Accumulated Frames", &g_render_settings.pathtracer.max_accumulated_frames);
			ImGui::EndDisabled();

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Enable Environment", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_ENVIRONMENT_MAP);
			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Importance Sample Environment Map", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_ENVIRONMENT_MIS);

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Enable Luminance Clamp", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_LUMINANCE_CLAMP);
			ImGui::BeginDisabled(!(g_render_settings.pathtracer.flags & Pathtracer::FLAG_LUMINANCE_CLAMP));
			g_render_settings.pathtracer.reset |= ImGui::InputFloat("Luminance Clamp", &g_render_settings.pathtracer.luminance_clamp);
			ImGui::EndDisabled();

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Material Diffuse White", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_MATERIAL_DIFFUSE_WHITE);
			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Use Geometric Normal", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_MATERIAL_USE_GEOMETRIC_NORMALS);
			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Use Multiple Importance Sampling", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_MATERIAL_MIS);

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Show NAN", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_SHOW_NAN);
			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Show INF", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_SHOW_INF);

			g_render_settings.pathtracer.reset |= ImGui::BitflagCheckbox("Shading Normal Adaptation", &g_render_settings.pathtracer.flags, Pathtracer::FLAG_SHADING_NORMAL_ADAPTATION);
		}
		ImGui::EndSection();
	}
}

void DrawUi()
{
	ProfileZoneScoped();
	ImGuiID dockspace_id = ImGui::GetID("Dockspace");
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// Setup initial docking layout.
	if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
	{
		// Main panel.
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);
		
		// Left panel.
		ImGuiID dock_id_left = 0;
		ImGuiID dock_id_main = 0;
		ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, &dock_id_left, &dock_id_main);
		
		// Right panel.
		ImGuiID dock_id_right = 0;
		ImGui::DockBuilderSplitNode(dock_id_main, ImGuiDir_Right, 0.2f, &dock_id_right, &dock_id_main);
		ImGuiID dock_id_right_up = 0;
		ImGuiID dock_id_right_bottom = 0;
		ImGui::DockBuilderSplitNode(dock_id_right, ImGuiDir_Up, 0.5f, &dock_id_right_up, &dock_id_right_bottom);

		ImGui::DockBuilderDockWindow("Nodes", dock_id_right_up);
		ImGui::DockBuilderDockWindow("Scene", dock_id_left);
		ImGui::DockBuilderDockWindow("Graphics", dock_id_left);
		ImGui::DockBuilderDockWindow("Properties", dock_id_right_bottom);
		ImGui::DockBuilderFinish(dockspace_id);
	}
	ImGui::DockSpaceOverViewport(dockspace_id, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

	DrawMainMenuBar();

	if (ImGui::Begin("Nodes")) {
		DrawNodesPanel(&g_gltf, &g_context);
	}
	ImGui::End();

	if (ImGui::Begin("Scene"))
	{
		DrawScenePanel(&g_gltf, &g_context);
	}
	ImGui::End();

	if (ImGui::Begin("Graphics"))
	{
		DrawGraphicsPanel();
	}
	ImGui::End();

	if (ImGui::Begin("Properties")) {
		DrawPropertiesPanel(&g_gltf, &g_context);
	}
	ImGui::End();
}

void ProcessEvents()
{
	ProfileZoneScoped();
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
		ImGui_ImplSDL3_ProcessEvent(&event);
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
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	ImFontConfig font_config;
	font_config.SizePixels = 14.0 * g_window_scale;
	io.Fonts->AddFontDefaultVector(&font_config);

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

	g_gltf.Init(&renderer.resources);

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
		DebugDraw::RemoveExpiredVertices(delta_time);
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
		{
			ProfileZoneScopedN("Animate");
			g_context.animation_player.Tick(&g_gltf, delta_time);
		}
		{
			ProfileZoneScopedN("Global Transforms");
			g_gltf.CalculateGlobalTransforms(g_context.scene_id);
		}
		{
			ProfileZoneScopedN("ImGui Draw List");
			ImGui::Render();
		}
		{
			ProfileZoneScopedN("Draw Frame");
			renderer.DrawFrame(&g_gltf, g_context.scene_id, &camera, &g_render_settings);
		}
		g_render_settings.pathtracer.reset = false;
		ProfileMarkFrame();
    }

	// Wait for all outstanding GPU work to complete before releasing resources.
	renderer.WaitForOutstandingWork();
	renderer.upload_buffer.WaitForAllSubmissionsToComplete();
	renderer.resources.SavePipelineCache();
	g_gltf.Unload();

	// Release resources.
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

    return 0;
}
