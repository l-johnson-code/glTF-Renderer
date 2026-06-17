#include "GpuScene.h"

HRESULT GpuScene::Init(Gpu::Resources* resources)
{
	HRESULT result = S_OK;

	this->resources = resources;

	for (int i = 0; i < Config::FRAME_COUNT; i++) {
		Gpu::BufferDesc lights_desc = {
			.name = "Lights",
			.size = sizeof(Light) * MAX_LIGHTS,
			.flags = Gpu::BUFFER_FLAG_GENERATE_DESCRIPTOR | Gpu::BUFFER_FLAG_PERSISTENT_MAP,
			.structured_byte_stride = sizeof(Light),
			.heap_type = D3D12_HEAP_TYPE_UPLOAD,
		};
		result = resources->CreateBuffer(&lights_desc, &this->lights[i]);
		if (FAILED(result)) {
			Shutdown();
			return result;
		}

		Gpu::BufferDesc materials_desc = {
			.name = "Materials",
			.size = sizeof(Material) * MAX_MATERIALS,
			.flags = Gpu::BUFFER_FLAG_GENERATE_DESCRIPTOR | Gpu::BUFFER_FLAG_PERSISTENT_MAP,
			.structured_byte_stride = sizeof(Material),
			.heap_type = D3D12_HEAP_TYPE_UPLOAD,
		};
		result = resources->CreateBuffer(&materials_desc, &this->materials[i]);
		if (FAILED(result)) {
			Shutdown();
			return result;
		}
	}

	return S_OK;
}

void GpuScene::Update(Gltf* gltf, int scene)
{
	lights.Next();
	lights_staging.clear();
	gltf->TraverseScene(scene, [&](Gltf* gltf, int node_id) {
		const Gltf::Node& node = gltf->nodes[node_id];
		int light_id = node.light_id;
		if (light_id != -1) {
			const Gltf::Light& scene_light = gltf->lights[light_id];
			Light light;
			switch (scene_light.type) {
				case Gltf::Light::TYPE_POINT:
					light.type = Light::TYPE_POINT;
					break;
				case Gltf::Light::TYPE_SPOT:
					light.type = Light::TYPE_SPOT;
					break;
				case Gltf::Light::TYPE_DIRECTIONAL:
					light.type = Light::TYPE_DIRECTIONAL;
					break;
			}
			light.color = scene_light.color;
			light.intensity = scene_light.intensity;
			light.cutoff = scene_light.cutoff;
			light.position = node.global_transform[3];
			light.direction = glm::normalize(glm::inverseTranspose(node.global_transform) * glm::vec4(0.0, 0.0, -1.0, 0.0));
			light.inner_angle = scene_light.inner_angle;
			light.outer_angle = scene_light.outer_angle;
			lights_staging.emplace_back(light);
			if (lights.Size() == MAX_LIGHTS) {
				return;
			}
		}
	});
	if (lights_staging.data()) {
		memcpy(this->lights.Current().Pointer(), lights_staging.data(), lights_staging.size());
	}

	materials.Next();
	Material* materials_pointer = (Material*)materials.Current().Pointer();
	for (int i = 0; i < std::min((int)gltf->materials.size(), MAX_MATERIALS); i++) {
		materials_pointer[i] = Material(gltf->materials[i]);
	}
}

void GpuScene::Shutdown()
{
	if (this->resources) {
		for (int i = 0; i < Config::FRAME_COUNT; i++) {
			this->resources->FreeBuffer(&lights[i]);
			this->resources->FreeBuffer(&materials[i]);
		}
	}
	this->resources = nullptr;
}