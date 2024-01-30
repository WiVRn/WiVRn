/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "utils/magic_hash.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/ext/quaternion_float.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <vk/allocation.h>
#include <vulkan/vulkan_raii.hpp>
#include <ktxvulkan.h>

struct sampler_info
{
	vk::Filter mag_filter = vk::Filter::eLinear;
	vk::Filter min_filter = vk::Filter::eLinear;
	vk::SamplerMipmapMode min_filter_mipmap = vk::SamplerMipmapMode::eLinear;
	vk::SamplerAddressMode wrapS = vk::SamplerAddressMode::eRepeat;
	vk::SamplerAddressMode wrapT = vk::SamplerAddressMode::eRepeat;

	bool operator==(const sampler_info & other) const noexcept = default;
};

namespace std
{
template <>
struct hash<sampler_info> : utils::magic_hash<sampler_info>
{};
} // namespace std

class node_handle;

struct scene_data
{
	struct image
	{
		std::variant<image_allocation, std::pair<VkDevice, ktxVulkanTexture>> image_;

		vk::raii::ImageView image_view = nullptr;
		~image();
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
		bool blend_enable = false;
	};

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

	struct node
	{
		static constexpr size_t root_id = size_t(-1);

		size_t parent_id;
		std::optional<size_t> mesh_id;

		glm::vec3 position;
		glm::quat orientation;
		glm::vec3 scale;

		std::string name;
		bool visible;

		uint32_t layer_mask = 1;

		// std::vector<glm::mat4> bones_transform; // TODO: bones
	};

	// TODO: lights
	// TODO: skybox

	std::vector<scene_data::mesh> meshes;
	std::vector<scene_data::node> scene_objects;

	scene_data() = default;
	scene_data(const scene_data &) = delete;
	scene_data(scene_data &&) = default;
	scene_data & operator=(const scene_data &) = delete;
	scene_data & operator=(scene_data &&) = default;

	scene_data& import(scene_data && other, node_handle parent);
	scene_data& import(scene_data && other);

	node_handle new_node();
	node_handle find_node(std::string_view name);
	node_handle find_node(node_handle parent, std::string_view name);
	std::vector<node_handle> find_children(node_handle parent);

	std::shared_ptr<material> find_material(std::string_view name);
};

class scene_loader
{
	vk::raii::Device & device;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Queue & queue;
	uint32_t queue_family_index;
	std::shared_ptr<scene_data::material> default_material;

public:
	scene_loader(vk::raii::Device & device, vk::raii::PhysicalDevice physical_device, vk::raii::Queue & queue, uint32_t queue_family_index, std::shared_ptr<scene_data::material> default_material) :
	device(device),
	physical_device(physical_device),
	queue(queue),
	queue_family_index(queue_family_index),
	default_material(default_material)
	{}

	scene_data operator()(const std::filesystem::path & gltf_path);
};

class node_handle
{
	friend struct scene_data;
	size_t id = scene_data::node::root_id;
	scene_data * scene = nullptr;

public:
	node_handle() = default;
	node_handle(size_t id, scene_data * scene) : id(id), scene(scene) {}
	node_handle(const node_handle&) = default;
	node_handle& operator=(const node_handle&) = default;

	scene_data::node& operator*()
	{
		assert(scene != nullptr);
		assert(id < scene->scene_objects.size());
		return scene->scene_objects[id];
	}

	const scene_data::node& operator*() const
	{
		assert(scene != nullptr);
		assert(id < scene->scene_objects.size());
		return scene->scene_objects[id];
	}

	scene_data::node* operator->()
	{
		assert(scene != nullptr);
		assert(id < scene->scene_objects.size());
		return &scene->scene_objects[id];
	}

	const scene_data::node* operator->() const
	{
		assert(scene != nullptr);
		assert(id < scene->scene_objects.size());
		return &scene->scene_objects[id];
	}

	node_handle parent()
	{
		assert(scene != nullptr);
		assert(id < scene->scene_objects.size());

		return {scene->scene_objects[id].parent_id, scene};
	}

	const node_handle parent() const
	{
		assert(scene != nullptr);
		assert(id < scene->scene_objects.size());

		return {scene->scene_objects[id].parent_id, scene};
	}

	operator bool() const
	{
		return id != scene_data::node::root_id;
	}
};
