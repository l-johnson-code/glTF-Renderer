#include "Gltf.h"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <vector>

#include <directx/d3d12.h>
#include <directx/d3dx12_core.h>
#include <directx/dxgiformat.h>
#include <glm/gtc/packing.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include "Animation.h"
#include "DescriptorAllocator.h"
#include "UploadBuffer.h"
#include "TinyGltfTools.h"

void Gltf::TraverseScene(int scene, const std::function<void(Gltf*, int)>& lambda)
{
	for (auto node_id: scenes[scene].nodes) {
		TraverseNode(node_id, lambda);
	}
}

void Gltf::TraverseNode(int node_id, const std::function<void(Gltf*, int)>& lambda)
{
	assert((node_id >= 0) && (node_id < nodes.size()));
	lambda(this, node_id);
	for (int i = this->nodes[node_id].child; i != -1; i = this->nodes[i].sibling) {
		TraverseNode(i, lambda);
	}
}

void Gltf::Unload()
{
	scenes = std::vector<Scene>(1);
	
	// Need to explicitly free descriptors for all meshes and textures.
	for (Mesh& mesh: meshes) {
		for (Primitive& primitive: mesh.primitives) {
			primitive.mesh.Destroy(this->srv_uav_cbv_descriptors);
		}
	}
	for (DynamicPrimitives& dynamic: dynamic_primitives) {
		for (DynamicMesh& dynamic_mesh: dynamic.dynamic_meshes) {
			dynamic_mesh.Destroy(this->srv_uav_cbv_descriptors);
		}
	}
	for (Texture& texture: textures) {
		srv_uav_cbv_descriptors->Free(texture.descriptor);
		texture.descriptor = -1;
	}
	// We can free all dynamic samplers at once because this is the only class that uses them.
	if (sampler_descriptors) {
		sampler_descriptors->Reset();
	}
	
	cameras.resize(0);
	meshes.resize(0);
    materials.resize(0);
    nodes.resize(0);
    skins.resize(0);
    dynamic_primitives.resize(0);
    animations.resize(0);
    lights.resize(0);
    textures.resize(0);
}

void Gltf::LoadMeshes(tinygltf::Model* gltf, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	// Create meshes.
	this->meshes.resize(gltf->meshes.size());
	for (int i = 0; i < gltf->meshes.size(); i++) {
		LoadMesh(gltf, &gltf->meshes[i], device, upload_buffer, &this->meshes[i]);
	}
}

void Gltf::LoadMesh(tinygltf::Model* gltf, tinygltf::Mesh* gltf_mesh, ID3D12Device* device, UploadBuffer* upload_buffer, Mesh* mesh)
{
	mesh->name = gltf_mesh->name;
	mesh->primitives.resize(gltf_mesh->primitives.size());
	for (int i = 0; i < gltf_mesh->primitives.size(); i++) {
		LoadPrimitive(gltf, &gltf_mesh->primitives[i], device, upload_buffer, &mesh->primitives[i]);
	}
	mesh->weights.resize(gltf_mesh->weights.size());
	for (int i = 0; i < gltf_mesh->weights.size(); i++) {
		mesh->weights[i] = (float)gltf_mesh->weights[i];
	}
}

void Gltf::LoadPrimitive(tinygltf::Model* gltf, tinygltf::Primitive* gltf_primitive, ID3D12Device* device, UploadBuffer* upload_buffer, Primitive* primitive)
{
	::Mesh::Desc desc = {};

	// Get the primitive type.
	switch (gltf_primitive->mode) {
		case TINYGLTF_MODE_POINTS: { 
			desc.topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;  
		} break;
		case TINYGLTF_MODE_LINE: { 
			desc.topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST; 
		} break;
		case TINYGLTF_MODE_LINE_LOOP: { 
			SPDLOG_WARN("Unsupported Topology: Line Loop.");
			return;
		} break;
		case TINYGLTF_MODE_LINE_STRIP: { 
			desc.topology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP; 
		} break;
		case TINYGLTF_MODE_TRIANGLES: { 
			desc.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; 
		} break;
		case TINYGLTF_MODE_TRIANGLE_STRIP: { 
			desc.topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; 
		} break;
		case TINYGLTF_MODE_TRIANGLE_FAN: { 
			SPDLOG_WARN("Unsupported Topology: Triangle Fan.");
			return;
		} break;
	}

	desc.flags |= gltf_primitive->indices != -1 ? ::Mesh::FLAG_INDEX : 0;
	desc.flags |= gltf_primitive->attributes.contains("NORMAL") ? ::Mesh::FLAG_NORMAL : 0;
	desc.flags |= gltf_primitive->attributes.contains("TANGENT") ? ::Mesh::FLAG_TANGENT : 0;
	desc.flags |= gltf_primitive->attributes.contains("TEXCOORD_0") ? ::Mesh::FLAG_TEXCOORD_0 : 0;
	desc.flags |= gltf_primitive->attributes.contains("TEXCOORD_1") ? ::Mesh::FLAG_TEXCOORD_1 : 0;
	desc.flags |= gltf_primitive->attributes.contains("COLOR_0") ? ::Mesh::FLAG_COLOR : 0;
	desc.flags |= gltf_primitive->attributes.contains("JOINTS_0") && gltf_primitive->attributes.contains("WEIGHTS_0")? ::Mesh::FLAG_JOINT_WEIGHT : 0;

	if (desc.flags & ::Mesh::FLAG_INDEX) {
		desc.num_of_indices = gltf->accessors[gltf_primitive->indices].count;
		switch (gltf->accessors[gltf_primitive->indices].componentType) {
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
				desc.index_format = DXGI_FORMAT_R16_UINT;
				break;
			case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
				desc.index_format = DXGI_FORMAT_R32_UINT;
				break;
		}
	}

	desc.num_of_vertices = gltf->accessors[gltf_primitive->attributes["POSITION"]].count;

	primitive->mesh.Create(device, srv_uav_cbv_descriptors, &desc);

	// Begin uploading data.
	if (desc.flags & ::Mesh::FLAG_INDEX) {
		std::byte* dest = (std::byte*)primitive->mesh.QueueIndexUpdate(upload_buffer);
		tinygltf::Accessor* index_accessor = &gltf->accessors[gltf_primitive->indices];
		int component_type = index_accessor->componentType;
		if (component_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
			// DirectX 12 doesn't support 8 bit indices, so convert them to 16 bit.
			tinygltf::tools::Copy((glm::u16vec1*)dest, gltf, index_accessor);
		} else {
			tinygltf::tools::Copy(dest, gltf, index_accessor);
		}
	}

	tinygltf::Accessor* position_accessor = &gltf->accessors[gltf_primitive->attributes["POSITION"]];
	glm::vec3* dest = (glm::vec3*)primitive->mesh.QueuePositionUpdate(upload_buffer);
	tinygltf::tools::Copy(dest, gltf, position_accessor);

	if (desc.flags & ::Mesh::FLAG_NORMAL) {
		tinygltf::Accessor* normal_accessor = &gltf->accessors[gltf_primitive->attributes["NORMAL"]];
		glm::vec3* dest = (glm::vec3*)primitive->mesh.QueueNormalUpdate(upload_buffer);
		tinygltf::tools::Copy(dest, gltf, normal_accessor);
	}

	if (desc.flags & ::Mesh::FLAG_TANGENT) {
		tinygltf::Accessor* tangent_accessor = &gltf->accessors[gltf_primitive->attributes["TANGENT"]];
		glm::vec4* dest = (glm::vec4*)primitive->mesh.QueueTangentUpdate(upload_buffer);
		tinygltf::tools::Copy(dest, gltf, tangent_accessor);
	}

	if (desc.flags & ::Mesh::FLAG_TEXCOORD_0) {
		tinygltf::Accessor* texcoord_0_accessor = &gltf->accessors[gltf_primitive->attributes["TEXCOORD_0"]];
		glm::vec2* dest = (glm::vec2*)primitive->mesh.QueueTexcoord0Update(upload_buffer);
		tinygltf::tools::Copy(dest, gltf, texcoord_0_accessor);
	}

	if (desc.flags & ::Mesh::FLAG_TEXCOORD_1) {
		tinygltf::Accessor* texcoord_1_accessor = &gltf->accessors[gltf_primitive->attributes["TEXCOORD_1"]];
		glm::vec2* dest = (glm::vec2*)primitive->mesh.QueueTexcoord1Update(upload_buffer);
		tinygltf::tools::Copy(dest, gltf, texcoord_1_accessor);
	}

	if (desc.flags & ::Mesh::FLAG_COLOR) {
		tinygltf::Accessor* color_accessor = &gltf->accessors[gltf_primitive->attributes["COLOR_0"]];
		glm::vec4* dest = (glm::vec4*)primitive->mesh.QueueColorUpdate(upload_buffer);
		tinygltf::tools::Copy(dest, gltf, color_accessor);
	}

	// Create bone weights.
	if (desc.flags & ::Mesh::FLAG_JOINT_WEIGHT) {
		::Mesh::JointWeight* dest = (::Mesh::JointWeight*)primitive->mesh.QueueJointWeightUpdate(upload_buffer);
		tinygltf::Accessor* joint_accessor = &gltf->accessors[gltf_primitive->attributes["JOINTS_0"]];
		tinygltf::tools::Iterate<4, uint32_t>(gltf, joint_accessor, [&](int i, const glm::u32vec4& data) {
			dest[i].joints = data;
		});
		tinygltf::Accessor* weight_accessor = &gltf->accessors[gltf_primitive->attributes["WEIGHTS_0"]];
		tinygltf::tools::Iterate<4, float>(gltf, weight_accessor, [&](int i, const glm::vec4& data) {
			dest[i].weights = data;
		});
	}

	// The material id is incremented by 1 so that an id of 0 will use the default material.
	primitive->material_id = gltf_primitive->material + 1;
	
	// Create morph targets.
	primitive->targets.resize(gltf_primitive->targets.size());
	for (int i = 0; i < gltf_primitive->targets.size(); i++) {
		CreateMorphTarget(gltf, &gltf_primitive->targets[i], device, upload_buffer, primitive->mesh.num_of_vertices, &primitive->targets[i]);
	}
}

void Gltf::CreateMorphTarget(tinygltf::Model* gltf, std::map<std::string, int>* target, ID3D12Device* device, UploadBuffer* upload_buffer, int num_of_vertices, MorphTarget* morph_target)
{
	// Morph targets.
	MorphTarget::Desc desc = {};
	desc.num_of_vertices = num_of_vertices;
	desc.flags |= target->contains("POSITION") ? MorphTarget::FLAG_POSITION : 0;
	desc.flags |= target->contains("NORMAL") ? MorphTarget::FLAG_NORMAL : 0;
	desc.flags |= target->contains("TANGENT") ? MorphTarget::FLAG_TANGENT : 0;

	morph_target->Create(device, srv_uav_cbv_descriptors, &desc);

	if (desc.flags & MorphTarget::FLAG_POSITION) {
		glm::vec3* dest = (glm::vec3*)morph_target->QueuePositionUpdate(upload_buffer);
		tinygltf::Accessor* accessor = &gltf->accessors[target->at("POSITION")];
		tinygltf::tools::Copy(dest, gltf, accessor);
	}

	if (desc.flags & MorphTarget::FLAG_NORMAL) {
		glm::vec3* dest = (glm::vec3*)morph_target->QueueNormalUpdate(upload_buffer);
		tinygltf::Accessor* accessor = &gltf->accessors[target->at("NORMAL")];
		tinygltf::tools::Copy(dest, gltf, accessor);
	}

	if (desc.flags & MorphTarget::FLAG_TANGENT) {
		glm::vec3* dest = (glm::vec3*)morph_target->QueueTangentUpdate(upload_buffer);
		tinygltf::Accessor* accessor = &gltf->accessors[target->at("TANGENT")];
		tinygltf::tools::Copy(dest, gltf, accessor);
	}
}

void Gltf::GetTextureTransform(tinygltf::Value* gltf_value, int* tex_coord, glm::vec2* offset, float* rotation, glm::vec2* scale)
{
	*offset = glm::vec2(0.0, 0.0);
	*rotation = 0.0;
	*scale = glm::vec2(1.0, 1.0);
	
	if (!gltf_value->IsObject()) {
		return;
	}

	auto offset_value = gltf_value->Get("offset");
	if (offset_value.ArrayLen() == 2) {
		*offset = glm::vec2(offset_value.Get(0).GetNumberAsDouble(), offset_value.Get(1).GetNumberAsDouble());
	}
	auto rotation_value = gltf_value->Get("rotation");
	if (rotation_value.IsNumber()) {
		*rotation = rotation_value.GetNumberAsDouble();
	}
	auto scale_value = gltf_value->Get("scale");
	if (scale_value.ArrayLen() == 2) {
		*scale = glm::vec2(scale_value.Get(0).GetNumberAsDouble(), scale_value.Get(1).GetNumberAsDouble());
	}
	auto tex_coord_value = gltf_value->Get("texCoord");
	if (tex_coord_value.IsInt()) {
		int value = tex_coord_value.GetNumberAsInt();
		if (value >= 0 && value < ::Mesh::MAX_TEXCOORDS) {
			*tex_coord = value;
		}
	}
}

Gltf::Material::Texture Gltf::GetTexture(tinygltf::Model* gltf, int texture_index, int tex_coord, tinygltf::Value* texture_transform, bool srgb, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	Material::Texture material_texture;
	if (texture_index != -1) {
		tinygltf::Texture* texture = &gltf->textures[texture_index];
		int texture_source = texture->source;
		if (texture_source != -1) {
			if (this->textures[texture_source].descriptor == -1) {
				// Load the texture if not yet loaded.
				LoadTexture(gltf, texture_source, srgb, device, upload_buffer);
			}
			material_texture = {
				.texture = this->textures[texture_source].descriptor,
				.sampler = texture->sampler == -1 ? 0 : sampler_descriptors->GetAbsoluteIndex(texture->sampler),
				.tex_coord = tex_coord < ::Mesh::MAX_TEXCOORDS ? tex_coord : 0,
			};
			GetTextureTransform(texture_transform, &material_texture.tex_coord, &material_texture.offset, &material_texture.rotation, &material_texture.scale);
		} else {
			// TODO: Create a default magenta texture.
		}
	}
	return material_texture;
}

Gltf::Material::Texture Gltf::GetTexture(tinygltf::Model* gltf, tinygltf::TextureInfo* texture_info, bool srgb, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	return GetTexture(gltf, texture_info->index, texture_info->texCoord, &texture_info->extensions["KHR_texture_transform"], srgb, device, upload_buffer);
}

Gltf::Material::Texture Gltf::GetTexture(tinygltf::Model* gltf, tinygltf::NormalTextureInfo* texture_info, float* scale, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	*scale = texture_info->scale;
	return GetTexture(gltf, texture_info->index, texture_info->texCoord, &texture_info->extensions["KHR_texture_transform"], false, device, upload_buffer);
}

Gltf::Material::Texture Gltf::GetTexture(tinygltf::Model* gltf, tinygltf::OcclusionTextureInfo* texture_info, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	return GetTexture(gltf, texture_info->index, texture_info->texCoord, &texture_info->extensions["KHR_texture_transform"], false, device, upload_buffer);
}

Gltf::Material::Texture Gltf::GetTexture(tinygltf::Model* gltf, const tinygltf::Value* texture_info, float* scale, bool srgb, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	Material::Texture desc;

	if (!texture_info->IsObject()) {
		return desc;
	}

	auto index_value = texture_info->Get("index");
	int index = index_value.GetNumberAsInt();
	auto tex_coord_value = texture_info->Get("texCoord");
	int tex_coord = tex_coord_value.GetNumberAsInt();
	if (scale) {
		auto scale_value = texture_info->Get("scale");
		if (scale_value.IsNumber()) {
			*scale = scale_value.GetNumberAsDouble();
		}
	}
	auto extensions = texture_info->Get("extensions");
	tinygltf::Value transform_extension;
	if (extensions.IsObject()) {
		transform_extension = extensions.Get("KHR_texture_transform");
	}

	return GetTexture(gltf, index, tex_coord, &transform_extension, srgb, device, upload_buffer);
}

void Gltf::LoadMaterials(tinygltf::Model* gltf, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	materials.resize(gltf->materials.size() + 1);
	for (int i = 0; i < gltf->materials.size(); i++) {
		tinygltf::Material* tiny_gltf_material = &gltf->materials[i];

		// Material indexes are incremented so that we can put a default material at 0.
		Material& material = materials[i+1];

		// Normal map.
		tinygltf::NormalTextureInfo* normal_texture_info = &tiny_gltf_material->normalTexture;
		material.normal = GetTexture(gltf, normal_texture_info, &material.normal_map_scale, device, upload_buffer);

		// Albedo.
		tinygltf::TextureInfo* albedo_texture_info = &tiny_gltf_material->pbrMetallicRoughness.baseColorTexture;
		material.albedo = GetTexture(gltf, albedo_texture_info, true, device, upload_buffer);
		material.base_color_factor = glm::vec4(
			tiny_gltf_material->pbrMetallicRoughness.baseColorFactor[0],
			tiny_gltf_material->pbrMetallicRoughness.baseColorFactor[1],
			tiny_gltf_material->pbrMetallicRoughness.baseColorFactor[2],
			tiny_gltf_material->pbrMetallicRoughness.baseColorFactor[3]
		);

		// Metalness and roughness.
		tinygltf::TextureInfo* metallic_roughness_texture_info = &tiny_gltf_material->pbrMetallicRoughness.metallicRoughnessTexture;
		material.metallic_roughness = GetTexture(gltf, metallic_roughness_texture_info, false, device, upload_buffer);
		material.metalness_factor = tiny_gltf_material->pbrMetallicRoughness.metallicFactor;
		material.roughness_factor = tiny_gltf_material->pbrMetallicRoughness.roughnessFactor;

		// Occlusion.
		tinygltf::OcclusionTextureInfo* occlusion_texture_info = &tiny_gltf_material->occlusionTexture;
		material.occlusion = GetTexture(gltf, occlusion_texture_info, device, upload_buffer);

		// Emissive.
		tinygltf::TextureInfo* emissive_texture_info = &tiny_gltf_material->emissiveTexture;
		material.emissive = GetTexture(gltf, emissive_texture_info, true, device, upload_buffer);
		material.emissive_factor = glm::vec3(tiny_gltf_material->emissiveFactor[0], tiny_gltf_material->emissiveFactor[1], tiny_gltf_material->emissiveFactor[2]);

		// Alpha.
		if (tiny_gltf_material->alphaMode == "OPAQUE") {
			material.alpha_mode = Material::ALPHA_MODE_OPAQUE;
		} else if (tiny_gltf_material->alphaMode == "MASK") {
			material.alpha_mode = Material::ALPHA_MODE_MASK;
		} else if (tiny_gltf_material->alphaMode == "BLEND") {
			material.alpha_mode = Material::ALPHA_MODE_BLEND;
		}
		material.alpha_cutoff = tiny_gltf_material->alphaCutoff;

		// Double sided.
		if (tiny_gltf_material->doubleSided) {
			material.flags |= Material::FLAG_DOUBLE_SIDED;
		}
		
		// Anisotropy.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_anisotropy");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "anisotropyStrength", &material.anisotropy_strength);
				tinygltf::tools::GetValue(it->second, "anisotropyRotation", &material.anisotropy_rotation);
				material.anisotropy_texture = GetTexture(gltf, &it->second.Get("anisotropyTexture"), nullptr, false, device, upload_buffer);
			}
		}

		// Clearcoat.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_clearcoat");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "clearcoatFactor", &material.clearcoat_factor);
				tinygltf::tools::GetValue(it->second, "clearcoatRoughnessFactor", &material.clearcoat_roughness_factor);
				material.clearcoat_texture = GetTexture(gltf, &it->second.Get("clearcoatTexture"), nullptr, false, device, upload_buffer);
				material.clearcoat_roughness_texture = GetTexture(gltf, &it->second.Get("clearcoatRoughnessTexture"), nullptr, false, device, upload_buffer);
				material.clearcoat_normal_texture = GetTexture(gltf, &it->second.Get("clearcoatNormalTexture"), &material.clearcoat_normal_scale, false, device, upload_buffer);
			}
		}

		// Dispersion.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_dispersion");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "dispersion", &material.dispersion);
			}
		}

		// Emissive strength.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_emissive_strength");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "emissiveStrength", &material.emissive_strength);
			}
		}
		
		// Index of refraction.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_ior");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "ior", &material.ior);
			}
		}

		// Iridescence.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_iridescence");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "iridescenceFactor", &material.iridescence_factor);
				tinygltf::tools::GetValue(it->second, "iridescenceIor", &material.iridescence_ior);
				tinygltf::tools::GetValue(it->second, "iridescenceThicknessMinimum", &material.iridescence_thickness_minimum);
				tinygltf::tools::GetValue(it->second, "iridescenceThicknessMaximum", &material.iridescence_thickness_maximum);
				material.iridescence_texture = GetTexture(gltf, &it->second.Get("iridescenceTexture"), nullptr, false, device, upload_buffer);
				material.iridescence_thickness_texture = GetTexture(gltf, &it->second.Get("iridescenceThicknessTexture"), nullptr, false, device, upload_buffer);
			}
		}

		// Sheen.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_sheen");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "sheenColorFactor", &material.sheen_color_factor);
				tinygltf::tools::GetValue(it->second, "sheenRoughnessFactor", &material.sheen_roughness_factor);
				material.sheen_color_texture = GetTexture(gltf, &it->second.Get("sheenColorTexture"), nullptr, true, device, upload_buffer);
				material.sheen_roughness_texture = GetTexture(gltf, &it->second.Get("sheenRoughnessTexture"), nullptr, false, device, upload_buffer);
			}
		}

		// Specular.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_specular");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "specularFactor", &material.specular_factor);
				tinygltf::tools::GetValue(it->second, "specularColorFactor", &material.specular_color_factor);
				material.specular_texture = GetTexture(gltf, &it->second.Get("specularTexture"), nullptr, false, device, upload_buffer);
				material.specular_color_texture = GetTexture(gltf, &it->second.Get("specularColorTexture"), nullptr, true, device, upload_buffer);
			}
		}

		// Transmission.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_transmission");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "transmissionFactor", &material.transmission_factor);
				material.transmission_texture = GetTexture(gltf, &it->second.Get("transmissionTexture"), nullptr, false, device, upload_buffer);
			}
		}

		// Volume.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_volume");
			if (it != tiny_gltf_material->extensions.end()) {
				tinygltf::tools::GetValue(it->second, "thicknessFactor", &material.thickness_factor);
				material.thickness_texture = GetTexture(gltf, &it->second.Get("thicknessTexture"), nullptr, false, device, upload_buffer);
				tinygltf::tools::GetValue(it->second, "attenuationDistance", &material.attenuation_distance);
				tinygltf::tools::GetValue(it->second, "attenuationColor", &material.attenuation_color);
			}
		}

		// Unlit.
		{
			auto it = tiny_gltf_material->extensions.find("KHR_materials_unlit");
			if (it != tiny_gltf_material->extensions.end()) {
				material.flags |= Material::Flags::FLAG_UNLIT;
			}
		}
	}
}

void Gltf::LoadScenes(tinygltf::Model* gltf)
{
	this->scenes.resize(gltf->scenes.size());
	for (int i = 0; i < gltf->scenes.size(); i++) {
		this->scenes[i].name = gltf->scenes[i].name;
		this->scenes[i].nodes = gltf->scenes[i].nodes;
	}
}

void Gltf::LoadCameras(tinygltf::Model* gltf)
{
	for (auto gltf_camera: gltf->cameras) {
		Camera camera;
		if (gltf_camera.type == "Perspective") {
			camera.Perspective(gltf_camera.perspective.aspectRatio, gltf_camera.perspective.yfov, gltf_camera.perspective.zfar, gltf_camera.perspective.znear);
		} else if (gltf_camera.type == "Orthographic") {
			camera.Orthographic(gltf_camera.orthographic.xmag, gltf_camera.orthographic.ymag, gltf_camera.orthographic.zfar, gltf_camera.orthographic.znear);
		}
		this->cameras.emplace_back(camera);
	}
}

void Gltf::LoadNodes(tinygltf::Model* gltf)
{
	this->nodes.resize(gltf->nodes.size());

	for (int i = 0; i < gltf->nodes.size(); i++) {
		Node& node = this->nodes[i];
		tinygltf::Node& tiny_gltf_node = gltf->nodes[i];

		node.name = tiny_gltf_node.name;
		if (tiny_gltf_node.matrix.size() != 0) {
			glm::mat4x4 transform(
				glm::vec4(tiny_gltf_node.matrix[0], tiny_gltf_node.matrix[1], tiny_gltf_node.matrix[2], tiny_gltf_node.matrix[3]),
				glm::vec4(tiny_gltf_node.matrix[4], tiny_gltf_node.matrix[5], tiny_gltf_node.matrix[6], tiny_gltf_node.matrix[7]),
				glm::vec4(tiny_gltf_node.matrix[8], tiny_gltf_node.matrix[9], tiny_gltf_node.matrix[10], tiny_gltf_node.matrix[11]),
				glm::vec4(tiny_gltf_node.matrix[12], tiny_gltf_node.matrix[13], tiny_gltf_node.matrix[14], tiny_gltf_node.matrix[15])
			);
			glm::vec3 skew;
			glm::vec4 perspective;
			glm::decompose(transform, node.rest_transform.scale, node.rest_transform.rotation, node.rest_transform.translation, skew, perspective);
		} else {
			node.rest_transform.translation = tiny_gltf_node.translation.size() != 0 ? glm::vec3(tiny_gltf_node.translation[0], tiny_gltf_node.translation[1], tiny_gltf_node.translation[2]) : glm::vec3();
			node.rest_transform.rotation = tiny_gltf_node.rotation.size() != 0 ? glm::quat(tiny_gltf_node.rotation[0], tiny_gltf_node.rotation[1], tiny_gltf_node.rotation[2], tiny_gltf_node.rotation[3]) : glm::quat();
			node.rest_transform.scale = tiny_gltf_node.scale.size() != 0 ? glm::vec3(tiny_gltf_node.scale[0], tiny_gltf_node.scale[1], tiny_gltf_node.scale[2]) : glm::vec3(1., 1., 1.);
		}

		// Add any mesh instances.
		node.mesh_id = tiny_gltf_node.mesh;
		node.skin_id = tiny_gltf_node.skin;

		node.weights.resize(tiny_gltf_node.weights.size());
		for (int j = 0; j < tiny_gltf_node.weights.size(); j++) {
			node.weights[j] = tiny_gltf_node.weights[j];
		}
		if (node.mesh_id != -1) {
			node.current_weights.assign(meshes[node.mesh_id].primitives[0].targets.size(), 0.0f);
		}

		node.camera_id = tiny_gltf_node.camera;
		node.light_id = tiny_gltf_node.light;

		// Convert into a child-sibling binary tree.
		if (tiny_gltf_node.children.size() > 0) {
			node.child = tiny_gltf_node.children[0];
			for (int i = 1; i < tiny_gltf_node.children.size(); i++) {
				this->nodes[tiny_gltf_node.children[i - 1]].sibling = tiny_gltf_node.children[i];
			}
		}
	}
}

void Gltf::LoadAnimations(tinygltf::Model* gltf)
{
	for (int i = 0; i < gltf->animations.size(); i++) {
		tinygltf::Animation& tiny_gltf_animation = gltf->animations[i];
		Animation animation;
		animation.name = tiny_gltf_animation.name;
		for (int j = 0; j < tiny_gltf_animation.channels.size(); j++) {
			tinygltf::AnimationChannel* gltf_channel = &tiny_gltf_animation.channels[j];
			tinygltf::AnimationSampler* sampler = &tiny_gltf_animation.samplers[gltf_channel->sampler];
			LoadAnimationChannel(gltf, gltf_channel, sampler, &animation);
		}
		animations.push_back(animation);
	}
}

void Gltf::LoadAnimationChannel(tinygltf::Model* gltf, tinygltf::AnimationChannel* gltf_channel, tinygltf::AnimationSampler* sampler, Animation* animation)
{	
	// Get path.
	Animation::Channel channel;
	channel.node_id = gltf_channel->target_node;
	if (gltf_channel->target_path == "rotation") {
		channel.path = Animation::Channel::PATH_ROTATION;
	} else if (gltf_channel->target_path == "translation") {
		channel.path = Animation::Channel::PATH_TRANSLATION;
	} else if (gltf_channel->target_path == "scale") {
		channel.path = Animation::Channel::PATH_SCALE;
	} else if (gltf_channel->target_path == "weights") {
		channel.path = Animation::Channel::PATH_WEIGHTS;
	} else {
		return;
	}

	// Get interpolation mode.
	if (sampler->interpolation == "STEP") {
		channel.interpolation_mode = Animation::Channel::INTERPOLATION_MODE_STEP;
	} else if (sampler->interpolation == "LINEAR") {
		channel.interpolation_mode = Animation::Channel::INTERPOLATION_MODE_LINEAR;
	} else if (sampler->interpolation == "CUBICSPLINE") {
		channel.interpolation_mode = Animation::Channel::INTERPOLATION_MODE_CUBIC_SPLINE;
	} else {
		return;
	}

	// Get keyframe times.
	const tinygltf::Accessor* input_accessor = &gltf->accessors[sampler->input];
	channel.times.resize(input_accessor->count);
	tinygltf::tools::Copy((glm::vec<1, float>*)channel.times.data(), gltf, input_accessor);

	// Get start and end time.
	float start_time = input_accessor->minValues[0];
	float end_time = input_accessor->maxValues[0];

	// Get transforms.
	tinygltf::Accessor* output_accessor = &gltf->accessors[sampler->output];
	int channel_width = 0;
	switch (output_accessor->componentType) {
		case TINYGLTF_COMPONENT_TYPE_FLOAT: {
			channel.format = Animation::Channel::FORMAT_FLOAT;
		} break;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
			channel.format = Animation::Channel::FORMAT_UNORM_16;
		} break;
		case TINYGLTF_COMPONENT_TYPE_SHORT: {
			channel.format = Animation::Channel::FORMAT_SNORM_8;
		} break;
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: {
			channel.format = Animation::Channel::FORMAT_UNORM_8;
		};
		case TINYGLTF_COMPONENT_TYPE_BYTE: {
			channel.format = Animation::Channel::FORMAT_SNORM_8;	
		} break;
		default: {
			return;
		} break;
	};
	switch (channel.path) {
        case Animation::Channel::PATH_WEIGHTS: {
        	channel_width = gltf->meshes[gltf->nodes[channel.node_id].mesh].primitives[0].targets.size();
        } break;
        case Animation::Channel::PATH_TRANSLATION:
        case Animation::Channel::PATH_SCALE: {
            channel_width = 3;
        } break;
        case Animation::Channel::PATH_ROTATION: {
            channel_width = 4;
        } break;
    }
	channel.width = channel_width;
	int component_size = tinygltf::GetComponentSizeInBytes(output_accessor->componentType);
	int num_of_values = channel_width * output_accessor->count;
	if (channel.path == Animation::Channel::PATH_WEIGHTS) {
		num_of_values = output_accessor->count;
	}
	if (channel.interpolation_mode == Animation::Channel::INTERPOLATION_MODE_CUBIC_SPLINE) {
		num_of_values *= 3;
	}
	channel.transforms.resize(num_of_values * component_size);
	tinygltf::tools::Copy(channel.transforms.data(), gltf, output_accessor);

	animation->channels.push_back(channel);
	animation->length = std::max(animation->length, end_time);
}

void Gltf::LoadSkins(tinygltf::Model* gltf)
{
	for (int i = 0; i < gltf->skins.size(); i++) {
		Skin skin;

		tinygltf::Skin* gltf_skin = &gltf->skins[i];

		// Joints.
		for (int j = 0; j < gltf_skin->joints.size(); j++) {
			skin.joints.push_back(gltf_skin->joints[j]);
		}

		// Inverse bind matrices.
		skin.inverse_bind_poses.resize(gltf_skin->joints.size());
		if (gltf_skin->inverseBindMatrices != -1) {
			tinygltf::Accessor* accessor = &gltf->accessors[gltf_skin->inverseBindMatrices];
			tinygltf::tools::Copy((std::byte*)skin.inverse_bind_poses.data(), gltf, accessor);
		} else {
			// TODO: Save memory by not creating a bind pose if none are found.
			for (int j = 0; j < gltf_skin->joints.size(); j++) {
				skin.inverse_bind_poses.push_back(glm::mat4x4(1.0));
			}
		}

		this->skins.emplace_back(skin);
	}
}

void Gltf::LoadSamplers(tinygltf::Model* gltf)
{
	for (int i = 0; i < gltf->samplers.size(); i++) {
		const tinygltf::Sampler& gltf_sampler = gltf->samplers[i];
		D3D12_SAMPLER_DESC sampler_desc = {
			.Filter = tinygltf::tools::TextureFilterConversion(gltf_sampler.minFilter, gltf_sampler.magFilter),
			.AddressU = tinygltf::tools::TextureAddressConversion(gltf_sampler.wrapS),
			.AddressV = tinygltf::tools::TextureAddressConversion(gltf_sampler.wrapT),
			.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			.MinLOD = 0.0f,
			.MaxLOD = (gltf_sampler.minFilter == TINYGLTF_TEXTURE_FILTER_NEAREST) || (gltf_sampler.minFilter == TINYGLTF_TEXTURE_FILTER_LINEAR) ? 0.0f : std::numeric_limits<float>::max(),
		};
		sampler_descriptors->CreateSampler(sampler_descriptors->GetAbsoluteIndex(i), &sampler_desc);
	}
}

void Gltf::LoadLights(tinygltf::Model* gltf)
{
	lights.resize(gltf->lights.size());
	for (int i = 0; i < gltf->lights.size(); i++) {
		const tinygltf::Light& tiny_gltf_light = gltf->lights[i];
		Light& light = lights[i];
		if (tiny_gltf_light.type == "directional") {
			light.type = Light::TYPE_DIRECTIONAL;
		} else if (tiny_gltf_light.type == "point") {
			light.type = Light::TYPE_POINT;
		} else if (tiny_gltf_light.type == "spot") {
			light.type = Light::TYPE_SPOT;
		}
		light.intensity = tiny_gltf_light.intensity;
		light.cutoff = tiny_gltf_light.range;
		if (tiny_gltf_light.color.size() == 3) {
			light.color.x = tiny_gltf_light.color[0];
			light.color.y = tiny_gltf_light.color[1];
			light.color.z = tiny_gltf_light.color[2];
		} else {
			light.color = glm::vec3(1.0, 1.0, 1.0);
		}
		light.inner_angle = tiny_gltf_light.spot.innerConeAngle;
		light.outer_angle = tiny_gltf_light.spot.outerConeAngle;
	}
}

void Gltf::Init(CbvSrvUavPool* srv_uav_cbv_descriptors, SamplerStack* sampler_descriptors)
{
	this->srv_uav_cbv_descriptors = srv_uav_cbv_descriptors;
	this->sampler_descriptors = sampler_descriptors;
}

bool Gltf::LoadFromGltf(const char* filepath, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	tinygltf::TinyGLTF gltf;
	tinygltf::Model model;
	std::string error, warning;
	bool result = false;

	std::filesystem::path path(filepath);
	if (path.extension() == ".glb") {
		result = gltf.LoadBinaryFromFile(&model, &error, &warning, filepath);
	} else if (path.extension() == ".gltf") {
		result = gltf.LoadASCIIFromFile(&model, &error, &warning, filepath);
	} else {
		Unload();
		result = false;
	}
	if (!error.empty()) {
		SPDLOG_ERROR(error);
	}
	if (!warning.empty()) {
		SPDLOG_WARN(warning);
	}
	if (!result) {
		Unload();
		return false;
	}
	filename = path.filename().string();

	// Check for any unsupported extensions.
	for (auto extension: model.extensionsRequired) {
		if (
			extension != "KHR_lights_punctual" && 
			extension != "KHR_texture_transform" &&
			extension != "KHR_materials_ior" &&
			extension != "KHR_materials_specular" &&
			extension != "KHR_materials_anisotropy" &&
			extension != "KHR_materials_sheen"
		) {
			return false;
		}
	}

	LoadSamplers(&model);
	ReserveTextures(&model);
	LoadMeshes(&model, device, upload_buffer);
	LoadMaterials(&model, device, upload_buffer);
	LoadScenes(&model);
	LoadNodes(&model);
	LoadSkins(&model);
	LoadAnimations(&model);
	LoadLights(&model);
	CreateDynamicMesh(device);

	return true;
}

void Gltf::CreateDynamicMesh(ID3D12Device* device)
{
	// Create dynamic mesh instances for any meshes that are skinned or have morph weights.
    for (int i = 0; i < this->nodes.size(); i++) {
		Node& node = nodes[i];

        if (node.skin_id == -1 && node.current_weights.size() == 0) {
            this->nodes[i].dynamic_mesh = -1;
            continue;
        }

		const std::vector<Primitive>& primitives = this->meshes[node.mesh_id].primitives;
		DynamicPrimitives dynamic;
		dynamic.dynamic_meshes.resize(primitives.size());
		for (int j = 0; j < primitives.size(); j++) {
        	DynamicMesh dynamic_mesh;
			DynamicMesh::Desc desc = {};
			desc.num_of_vertices = primitives[j].mesh.num_of_vertices;
			desc.flags = DynamicMesh::FLAG_POSITION;
			if (primitives[j].mesh.flags & ::Mesh::FLAG_NORMAL) {
				desc.flags |= DynamicMesh::FLAG_NORMAL;
			}
			if (primitives[j].mesh.flags & ::Mesh::FLAG_TANGENT) {
				desc.flags |= DynamicMesh::FLAG_TANGENT;
			}
        	dynamic.dynamic_meshes[j].Create(device, srv_uav_cbv_descriptors, &desc);
		}
		dynamic_primitives.push_back(dynamic);
		node.dynamic_mesh = dynamic_primitives.size() - 1;
    }
}

void Gltf::ApplyRestTransforms()
{
    for (Node& node: nodes) {
        node.local_transform = node.rest_transform;
		if (node.weights.size() > 0) {
			node.current_weights = node.weights;
		} else if (node.mesh_id != -1 && this->meshes[node.mesh_id].weights.size() > 0) { 
			node.current_weights = this->meshes[node.mesh_id].weights;
		} else {
			node.current_weights.assign(node.current_weights.size(), 0.0f);
		}
    }
}

void Gltf::Animate(Animation* animation, float time)
{
    ApplyRestTransforms();
    for (Animation::Channel& channel: animation->channels) {
        int target = channel.node_id;
        switch (channel.path) {
            case Animation::Channel::PATH_TRANSLATION: {
                channel.GetTransform(time, &nodes[target].local_transform.translation.x);
            } break;
            case Animation::Channel::PATH_ROTATION: {
                channel.GetTransform(time, &nodes[target].local_transform.rotation.x);
            } break;
            case Animation::Channel::PATH_SCALE: {
                channel.GetTransform(time, &nodes[target].local_transform.scale.x);
            } break;
			case Animation::Channel::PATH_WEIGHTS: {
				channel.GetTransform(time, nodes[target].current_weights.data());
			} break;
        }
    }
}

void Gltf::CalculateGlobalTransforms(int scene)
{
	glm::mat4x4 coordinate_system_transform = glm::mat4x4(
		1., 0., 0., 0.,
		0., 0., 1., 0.,
		0., -1., 0., 0.,
		0., 0., 0., 1.
	);
	for (int i = 0; i < scenes[scene].nodes.size(); i++) {
    	CalculateGlobalTransforms(&this->nodes[this->scenes[scene].nodes[i]], coordinate_system_transform);
	}
}

void Gltf::CalculateGlobalTransforms(Gltf::Node* node, glm::mat4x4 parent_global_transform)
{
    // Calculate the global transform for this node.
    node->previous_global_transform = node->global_transform;
    node->global_transform = parent_global_transform;
	node->global_transform *= glm::translate(node->local_transform.translation);
	node->global_transform *= glm::mat4_cast(node->local_transform.rotation);
	node->global_transform *= glm::scale(node->local_transform.scale);

    // Iterate through this nodes children.
    for (int i = node->child; i != -1; i = this->nodes[i].sibling) {
		CalculateGlobalTransforms(&this->nodes[i], node->global_transform);
    }
}

void Gltf::ReserveTextures(tinygltf::Model* gltf)
{
	this->textures.resize(gltf->images.size());
}

void Gltf::LoadTexture(tinygltf::Model* gltf, int slot, bool srgb, ID3D12Device* device, UploadBuffer* upload_buffer)
{
	this->textures.reserve(slot + 1);

	tinygltf::Image& image = gltf->images[slot];
	// Only support RGBA 8 bit images.
	assert(image.component == 4);
	assert(image.bits == 8);
	
	// Create the resource.
	DXGI_FORMAT format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	CD3DX12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Tex2D(format, image.width, image.height, 1, 1);
	CD3DX12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE_DEFAULT);
	HRESULT result = device->CreateCommittedResource(&heap_properties, D3D12_HEAP_FLAG_NONE, &resource_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(this->textures[slot].resource.ReleaseAndGetAddressOf()));
	assert(result == S_OK);
	// TODO: Support full UTF-8 instead of just ASCII. 
	result = textures[slot].resource->SetPrivateData(WKPDID_D3DDebugObjectName, image.name.size(), image.name.data());
	assert(result == S_OK);

	// Create the descriptor.
	textures[slot].descriptor = srv_uav_cbv_descriptors->Allocate();
	srv_uav_cbv_descriptors->CreateSrv(textures[slot].descriptor, textures[slot].resource.Get(), nullptr);

	// Upload image to the GPU.
	uint32_t pitch = 0;
	std::byte* upload_ptr = (std::byte*)upload_buffer->QueueTextureUpload(format, image.width, image.height, 1, textures[slot].resource.Get(), 0, &pitch);
	for (int i = 0; i < image.height; i++) {
		memcpy(upload_ptr + i * pitch, image.image.data() + i * image.width * 4, image.width * 4);
	}
}