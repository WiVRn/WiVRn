/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <array>
#include <memory>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "utils/magic_hash.h"
#include "vk/allocation.h"

namespace renderer
{
struct sampler_info
{
	vk::Filter mag_filter = vk::Filter::eLinear;
	vk::Filter min_filter = vk::Filter::eLinear;
	vk::SamplerMipmapMode min_filter_mipmap = vk::SamplerMipmapMode::eLinear;
	vk::SamplerAddressMode wrapS = vk::SamplerAddressMode::eRepeat;
	vk::SamplerAddressMode wrapT = vk::SamplerAddressMode::eRepeat;

	bool operator==(const sampler_info & other) const noexcept = default;
};

struct texture
{
	std::shared_ptr<vk::raii::ImageView> image_view;
	sampler_info sampler;
};

struct material
{
	struct gpu_data
	{
		glm::vec4 base_color_factor = glm::vec4(1, 1, 1, 1);
		glm::vec4 base_emissive_factor = glm::vec4(0, 0, 0, 0);
		float metallic_factor = 1;
		float roughness_factor = 1;
		float occlusion_strength = 0;
		float normal_scale = 0;

		uint32_t base_color_texcoord = 0;
		uint32_t metallic_roughness_texcoord = 0;
		uint32_t occlusion_texcoord = 0;
		uint32_t emissive_texcoord = 0;
		uint32_t normal_texcoord = 0;

		// TODO: add fastgltf::TextureTransform?
	};

	std::shared_ptr<texture> base_color_texture;
	std::shared_ptr<texture> metallic_roughness_texture;
	std::shared_ptr<texture> occlusion_texture;
	std::shared_ptr<texture> emissive_texture;
	std::shared_ptr<texture> normal_texture;

	// Disable back face culling with this material
	bool double_sided = true;

	// TODO transparency
	// fastgltf::AlphaMode alphaMode;
	// float alpha_cutoff;

	gpu_data staging;

	// Resources used by this material
	std::shared_ptr<buffer_allocation> buffer;
	size_t offset;

	// The descriptor set is managed by the scene renderer, it is updated whenever ds_dirty is true
	// Bindings 0-4: textures
	// Binding 5: uniform buffer
	std::shared_ptr<vk::raii::DescriptorSet> ds;

	// Set to true to update the descriptor set at the next frame
	bool ds_dirty;

	std::string name;
	std::string shader_name = "lit";
	bool blend_enable = true;
};

struct primitive
{
	bool indexed;
	uint32_t index_count;
	uint32_t vertex_count;
	vk::IndexType index_type;
	vk::DeviceSize index_offset;
	vk::DeviceSize vertex_offset;

	// See also material::double_sided
	vk::CullModeFlagBits cull_mode = vk::CullModeFlagBits::eNone;
	vk::FrontFace front_face = vk::FrontFace::eClockwise;
	vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;

	std::shared_ptr<material> material_;
};

struct mesh
{
	std::vector<primitive> primitives;
	std::shared_ptr<buffer_allocation> buffer;
};

// TODO move vertex class in .cpp, put vertex description in the primitive
struct vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec3 tangent;
	std::array<glm::vec2, 2> texcoord;
	glm::vec4 color;
	std::array<glm::vec4, 1> joints;
	std::array<glm::vec4, 1> weights;

	struct description
	{
		vk::VertexInputBindingDescription binding;
		std::vector<vk::VertexInputAttributeDescription> attributes;
		std::vector<std::string> attribute_names;

		vk::PipelineVertexInputStateCreateFlags flags{};
	};

	static description describe();
};
} // namespace renderer

namespace std
{
template <>
struct hash<renderer::sampler_info> : utils::magic_hash<renderer::sampler_info>
{};
} // namespace std

namespace components
{
struct node
{
	entt::entity parent = entt::null;
	std::string name;
	std::shared_ptr<renderer::mesh> mesh;

	glm::vec3 position = {0, 0, 0};
	glm::quat orientation = {1, 0, 0, 0};
	glm::vec3 scale = {1, 1, 1};
	bool visible = true;
	uint32_t layer_mask = -1;

	std::array<glm::vec4, 4> clipping_planes;

	// TODO: separate component?
	std::vector<std::pair<entt::entity, glm::mat4>> joints; // Node index, inverse bind matrix of each joint

	// For internal use by the renderer
	glm::mat4 transform_to_root;
	bool global_visible;
	bool reverse_side;
	uint32_t global_layer_mask;
};
} // namespace components

entt::entity find_node_by_name(entt::registry & scene, std::string_view name);
entt::entity find_node_by_name(entt::registry & scene, std::string_view name, entt::entity parent);
std::string get_node_name(const entt::registry & scene, entt::entity entity);
