#pragma once

#include "Gltf.h"

class GpuScene {

    public:

    HRESULT Init(Gpu::Resources* resources);
    void Update(Gltf* gltf, int scene_id);
    int LightCount() { return lights_staging.size(); };
    Gpu::Buffer& LightBuffer() { return lights.Current(); };
    Gpu::Buffer& MaterialBuffer() { return materials.Current(); };
    void Shutdown();
    
    private:

    struct Light {
		enum Type {
            TYPE_POINT,
            TYPE_SPOT,
            TYPE_DIRECTIONAL,
        };
		int type;
		glm::vec3 position;
		float cutoff;
		glm::vec3 direction;
		float intensity;
		glm::vec3 color;
		float inner_angle;
		float outer_angle;
		std::byte pad[8];
	};

    struct TextureSample {
		int descriptor = -1;
        int sampler = 0;
        TextureSample() = default;
        TextureSample(const Gltf::Material::Texture& texture) {
            this->descriptor = texture.texture;
            this->sampler = texture.sampler;
        }
    };

    struct Material {
        uint32_t flags;
        int alpha_mode;
        float metalness_factor;
        float roughness_factor;
        glm::vec4 base_color_factor;
        float occlusion_factor;
        glm::vec3 emissive_factor;
        float alpha_cutoff;
        float ior;
        float normal_scale;
        TextureSample normal;
        TextureSample albedo;
        TextureSample metallic_roughness;
        TextureSample occlusion;
        TextureSample emissive;
        float specular_factor;
        glm::vec3 specular_color_factor;
        TextureSample specular;
        TextureSample specular_color;
        float clearcoat_factor;
        float clearcoat_roughness_factor;
        float clearcoat_normal_scale;
        TextureSample clearcoat;
        TextureSample clearcoat_roughness;
        TextureSample clearcoat_normal;
        float anisotropy_strength;
        float anisotropy_rotation;
        TextureSample anisotropy_texture;
        glm::vec3 sheen_color_factor;
        float sheen_roughness_factor;
        TextureSample sheen_color_texture;
        TextureSample sheen_roughness_texture;
        float transmission_factor = 0;
        float thickness_factor = 0;
        TextureSample transmission_texture;
        float attenuation_distance = 0;
        glm::vec3 attenuation_color = glm::vec3(1.0, 1.0, 1.0);
        TextureSample thickness_texture;
        Material() = default;
        Material(const Gltf::Material& material) {
            this->flags = material.flags;
			this->alpha_mode = material.alpha_mode;
            this->metalness_factor = material.metalness_factor;
            this->roughness_factor = material.roughness_factor;
            this->occlusion_factor = material.occlusion_factor;
            this->emissive_factor = material.emissive_strength * material.emissive_factor;
            this->base_color_factor = material.base_color_factor;
            this->normal_scale = material.normal_map_scale;
            this->normal = TextureSample(material.normal);
            this->albedo = TextureSample(material.albedo);
            this->metallic_roughness = TextureSample(material.metallic_roughness);
            this->occlusion = TextureSample(material.occlusion);
            this->emissive = TextureSample(material.emissive);
            this->alpha_cutoff = material.alpha_mode == Gltf::Material::ALPHA_MODE_MASK ? material.alpha_cutoff : 0.0f;
            this->ior = material.ior;
            this->specular_color_factor = material.specular_color_factor;
            this->specular_factor = material.specular_factor;
            this->specular = TextureSample(material.specular_texture);
            this->specular_color = TextureSample(material.specular_color_texture);
            this->clearcoat_factor = material.clearcoat_factor;
            this->clearcoat_roughness_factor = material.clearcoat_roughness_factor;
            this->clearcoat_normal_scale = material.clearcoat_normal_scale;
            this->clearcoat = TextureSample(material.clearcoat_texture);
            this->clearcoat_roughness = TextureSample(material.clearcoat_roughness_texture);
            this->clearcoat_normal = TextureSample(material.clearcoat_normal_texture);
            this->anisotropy_strength = material.anisotropy_strength;
            this->anisotropy_rotation = material.anisotropy_rotation;
            this->anisotropy_texture = TextureSample(material.anisotropy_texture);
            this->sheen_color_factor = material.sheen_color_factor;
            this->sheen_roughness_factor = material.sheen_roughness_factor;
            this->sheen_color_texture = TextureSample(material.sheen_color_texture);
            this->sheen_roughness_texture = TextureSample(material.sheen_roughness_texture);
            this->transmission_factor = material.transmission_factor;
            this->transmission_texture = TextureSample(material.transmission_texture);
            this->thickness_factor = material.thickness_factor;
            this->attenuation_distance = material.attenuation_distance;
            this->attenuation_color = material.attenuation_color;
            this->thickness_texture = TextureSample(material.thickness_texture);
        }
    };

    static constexpr int MAX_LIGHTS = 10;
    static constexpr int MAX_MATERIALS = 100;

    Gpu::Resources* resources = nullptr;

	std::vector<Light> lights_staging;
    MultiBuffer<Gpu::Buffer, Config::FRAME_COUNT> lights;
    
	MultiBuffer<Gpu::Buffer, Config::FRAME_COUNT> materials;
};