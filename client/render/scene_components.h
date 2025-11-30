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
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>
#include <vulkan/vulkan_raii.hpp>

#include "utils/magic_hash.h"
#include "vertex_layout.h"
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
	struct alignas(16) texture_info
	{
		alignas(4) uint32_t texcoord = 0;
		alignas(4) float rotation = 0;
		alignas(8) glm::vec2 offset = {0, 0};
		alignas(8) glm::vec2 scale = {1, 1};
	};

	struct gpu_data
	{
		glm::vec4 base_color_factor = glm::vec4(1, 1, 1, 1);
		glm::vec4 base_emissive_factor = glm::vec4(0, 0, 0, 0);
		float metallic_factor = 1;
		float roughness_factor = 1;
		float occlusion_strength = 0;
		float normal_scale = 1;
		float alpha_cutoff = 0.5;

		texture_info base_color;
		texture_info metallic_roughness;
		texture_info occlusion;
		texture_info emissive;
		texture_info normal;
	};

	std::shared_ptr<texture> base_color_texture;
	std::shared_ptr<texture> metallic_roughness_texture;
	std::shared_ptr<texture> occlusion_texture;
	std::shared_ptr<texture> emissive_texture;
	std::shared_ptr<texture> normal_texture;

	// Disable back face culling with this material
	bool double_sided = true;

	bool blend_enable = false;
	bool depth_test_enable = true;
	bool depth_write_enable = true;

	gpu_data staging;

	// Resources used by this material
	std::shared_ptr<buffer_allocation> buffer;
	size_t offset;

	std::string name;
	std::string fragment_shader_name = "lit.frag";
};

struct primitive
{
	bool indexed;
	uint32_t index_count;
	uint32_t vertex_count;
	vk::IndexType index_type;
	vk::DeviceSize index_offset;
	std::vector<vk::DeviceSize> vertex_offset; // TODO: inplace_vector
	vertex_layout layout;

	glm::vec3 obb_min;
	glm::vec3 obb_max;

	std::string vertex_shader;

	// See also material::double_sided
	vk::CullModeFlagBits cull_mode = vk::CullModeFlagBits::eNone;
	vk::FrontFace front_face = vk::FrontFace::eClockwise;
	vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;

	std::shared_ptr<material> material_;
};

// TODO make sure the mesh cannot be changed while it is used
struct mesh
{
	std::vector<primitive> primitives;
	std::shared_ptr<buffer_allocation> buffer;
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

struct animation_track_base
{
	enum class interpolation_t
	{
		step,
		linear,
		cubic_spline,
	};

	entt::entity target;
	interpolation_t interpolation;
};

template <auto node::* Field>
struct animation_track_impl : animation_track_base
{
	static const constexpr auto field = Field;
	using type = std::remove_reference_t<decltype(std::declval<node>().*field)>;

	std::vector<float> timestamp;
	std::vector<type> value;
};

using animation_track_position = animation_track_impl<&node::position>;
using animation_track_orientation = animation_track_impl<&node::orientation>;
using animation_track_scale = animation_track_impl<&node::scale>;

static_assert(std::is_same_v<animation_track_position::type, glm::vec3>);
static_assert(std::is_same_v<animation_track_orientation::type, glm::quat>);
static_assert(std::is_same_v<animation_track_scale::type, glm::vec3>);

struct animation
{
	std::string name;
	std::vector<
	        std::variant<
	                animation_track_position,
	                animation_track_orientation,
	                animation_track_scale>>
	        tracks;

	float duration = 0;

	float current_time = 0;
	bool playing = true;
	bool looping = true;
};

} // namespace components

entt::entity find_node_by_name(entt::registry & scene, std::string_view name);
entt::entity find_node_by_name(entt::registry & scene, std::string_view name, entt::entity parent);
std::string get_node_name(const entt::registry & scene, entt::entity entity);
