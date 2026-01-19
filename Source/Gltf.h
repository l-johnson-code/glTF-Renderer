#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <tinygltf/tiny_gltf.h>

#include "Animation.h"
#include "Camera.h"
#include "DescriptorAllocator.h"
#include "Mesh.h"
#include "RayTracingAccelerationStructure.h"
#include "UploadBuffer.h"

class Gltf {
    
    public:

    struct Trs {
        glm::vec3 translation;
        glm::quat rotation;
        glm::vec3 scale;
    };

    struct Node {
        std::string name;
        int child = -1;
        int sibling = -1;
        int mesh_id = -1;
        int skin_id = -1;
        int dynamic_mesh = -1;
        int camera_id = -1;
        int light_id = -1;
        Trs rest_transform;
        Trs local_transform;
        glm::mat4x4 global_transform;
        glm::mat4x4 previous_global_transform;
        std::vector<float> weights;
        std::vector<float> current_weights;
    };

    struct Scene {
        std::string name;
        std::vector<int> nodes;
    };

    struct Light {
        enum Type {
            TYPE_POINT,
            TYPE_SPOT,
            TYPE_DIRECTIONAL,
        };
        Type type;
        glm::vec3 color;
        float intensity;
        float cutoff;
        float inner_angle;
        float outer_angle;
    };

    struct Primitive {
        Mesh mesh;
        RaytracingAccelerationStructure::Blas blas;
        int material_id = 0;
        std::vector<MorphTarget> targets;
        std::vector<float> weights;
    };

    struct Mesh {
        std::string name;
        std::vector<Primitive> primitives;
        std::vector<float> weights;
    };

    struct DynamicPrimitives {
        std::vector<DynamicMesh> dynamic_meshes;
        std::vector<RaytracingAccelerationStructure::DynamicBlas> dynamic_blases;
    };

    struct Skin {
        std::vector<glm::mat4x4> inverse_bind_poses;
        std::vector<uint32_t> joints;
    };

    struct Material {

        enum Flags {
            FLAG_NONE = 0,
            FLAG_DOUBLE_SIDED = 1 << 0,
            FLAG_UNLIT = 1 << 1,
        };

        enum AlphaMode {
            ALPHA_MODE_OPAQUE,
            ALPHA_MODE_MASK,
            ALPHA_MODE_BLEND,
        };

        struct Texture {
            int texture = -1;
            int sampler = 0;
            int tex_coord = 0;
            glm::vec2 offset = glm::vec2(0.0);
            glm::vec2 scale = glm::vec2(1.0);
            float rotation = 0.0;
        };

        uint32_t flags = FLAG_NONE;

        glm::vec4 base_color_factor = glm::vec4(1.0);
        float metalness_factor = 1.0;
        float roughness_factor = 1.0;
        float occlusion_factor = 1.0;
        glm::vec3 emissive_factor = glm::vec3(0.0);
        float normal_map_scale = 1.0;
        Texture albedo;
        Texture normal;
        Texture metallic_roughness;
        Texture occlusion;
        Texture emissive;
        AlphaMode alpha_mode = ALPHA_MODE_OPAQUE;
        float alpha_cutoff = 0.5;

        // Anisotropy.
        float anisotropy_strength = 0.0;
        float anisotropy_rotation = 0.0;
        Texture anisotropy_texture;

        // Clearcoat.
        float clearcoat_factor = 0.0;
        Texture clearcoat_texture;
        float clearcoat_roughness_factor = 0.0;
        Texture clearcoat_roughness_texture;
        float clearcoat_normal_scale = 1.0;
        Texture clearcoat_normal_texture;

        // Dispersion.
        float dispersion = 0.0;

        // Emissive strength.
        float emissive_strength = 1.0;
        
        // Index of refraction.
        float ior = 1.5;

        // Iridescence.
        float iridescence_factor = 0.0;
        Texture iridescence_texture;
        float iridescence_ior = 1.3;
        float iridescence_thickness_minimum = 100.0;
        float iridescence_thickness_maximum = 400.0;
        Texture iridescence_thickness_texture;

        // Sheen.
        glm::vec3 sheen_color_factor = glm::vec3(0.0);
        Texture sheen_color_texture;
        float sheen_roughness_factor = 0.0;
        Texture sheen_roughness_texture;

        // Specular.
        float specular_factor = 1.0;
        Texture specular_texture;
        glm::vec3 specular_color_factor = glm::vec3(1.0);
        Texture specular_color_texture;

        // Transmission.
        float transmission_factor = 0.0;
        Texture transmission_texture;

        // Volume.
        float thickness_factor = 0;
        float attenuation_distance = 0;
        glm::vec3 attenuation_color = glm::vec3(1.0);
        Texture thickness_texture;
    };

    struct Texture {
        std::string name;
        int descriptor = -1;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;

        ~Texture() { assert(descriptor == -1); }
    };

    std::string filename;
    std::vector<Camera> cameras;
    std::vector<Scene> scenes = std::vector<Scene>(1);
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Node> nodes;
    std::vector<Skin> skins;
    std::vector<DynamicPrimitives> dynamic_primitives;
    std::vector<Animation> animations;
    std::vector<Light> lights;
    std::vector<Texture> textures;
    
    void Init(DescriptorPool* srv_uav_cbv_descriptors, DescriptorStack* sampler_descriptors);
    bool LoadFromGltf(const char* filepath, ID3D12Device* device, UploadBuffer* upload_buffer);
    void Unload();
    void ApplyRestTransforms();
    void CalculateGlobalTransforms(int scene);
    void Animate(Animation* animation, float time);
    void TraverseScene(int scene, const std::function<void(Gltf*, int)>& lambda);
    void TraverseNode(int node, const std::function<void(Gltf*, int)>& lambda);
    
    private:

    DescriptorPool* srv_uav_cbv_descriptors;
    DescriptorStack* sampler_descriptors;

    void LoadMeshes(tinygltf::Model* gltf, ID3D12Device* device, UploadBuffer* upload_buffer);
    void LoadMesh(tinygltf::Model* gltf, tinygltf::Mesh* gltf_mesh, ID3D12Device* device, UploadBuffer* upload_buffer, Mesh* mesh);
    void LoadPrimitive(tinygltf::Model* gltf, tinygltf::Primitive* gltf_primitive, ID3D12Device* device, UploadBuffer* upload_buffer, Primitive* primitive);
    void CreateMorphTarget(tinygltf::Model* gltf, std::map<std::string, int>* target, ID3D12Device* device, UploadBuffer* upload_buffer, int num_of_vertices, MorphTarget* morph_target);
    void GetTextureTransform(tinygltf::Value* gltf_value, int* tex_coord, glm::vec2* offset, float* rotation, glm::vec2* scale);
    Material::Texture GetTexture(tinygltf::Model* gltf, int texture_index, int tex_coord, tinygltf::Value* extensions, bool srgb, ID3D12Device* device, UploadBuffer* upload_buffer);
    Material::Texture GetTexture(tinygltf::Model* gltf, tinygltf::TextureInfo* texture_info, bool srgb, ID3D12Device* device, UploadBuffer* upload_buffer);
    Material::Texture GetTexture(tinygltf::Model* gltf, tinygltf::NormalTextureInfo* texture_info, float* scale, ID3D12Device* device, UploadBuffer* upload_buffer);
    Material::Texture GetTexture(tinygltf::Model* gltf, tinygltf::OcclusionTextureInfo* texture_info, ID3D12Device* device, UploadBuffer* upload_buffer);
    Material::Texture GetTexture(tinygltf::Model* gltf, const tinygltf::Value* texture_info, float* scale, bool srgb, ID3D12Device* device, UploadBuffer* upload_buffer);
    void LoadMaterials(tinygltf::Model* gltf, ID3D12Device* device, UploadBuffer* upload_buffer);
    void LoadScenes(tinygltf::Model* gltf);
    void LoadCameras(tinygltf::Model* gltf);
    void LoadNodes(tinygltf::Model* gltf);
    void LoadAnimations(tinygltf::Model* gltf);
    void LoadAnimationChannel(tinygltf::Model* gltf, tinygltf::AnimationChannel* gltf_channel, tinygltf::AnimationSampler* sampler, Animation* animation);
    void LoadSkins(tinygltf::Model* gltf);
    void LoadSamplers(tinygltf::Model* gltf);
    void LoadLights(tinygltf::Model* gltf);
    void ReserveTextures(tinygltf::Model* gltf);
    void LoadTexture(tinygltf::Model* gltf, int slot, bool srgb, ID3D12Device* device, UploadBuffer* upload_buffer);
    void CreateDynamicMesh(ID3D12Device* device);
    int AddDynamicMesh(DynamicMesh* skinned_vertices);
    void CalculateGlobalTransforms(Node* node, glm::mat4x4 parent_global_transform);
};