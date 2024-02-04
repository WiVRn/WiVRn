/*
 * WiVRn VR streaming
 * Copyright (C) 2024 Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <unordered_map>
#include <vector>

#include "vk/allocation.h"

#include "render/growable_descriptor_pool.h"
#include "render/scene_data.h"

struct pipeline_info
{
	std::string shader_name;

	vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eNone;
	vk::FrontFace front_face = vk::FrontFace::eClockwise;
	vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
	bool blend_enable = false;

	bool operator==(const pipeline_info & other) const noexcept = default;
};

namespace std
{
template <>
struct hash<pipeline_info> : utils::magic_hash<pipeline_info>
{};
} // namespace std

class scene_renderer
{
public:
	// vk::raii::Instance& instance;
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device & device;
	vk::PhysicalDeviceProperties physical_device_properties;
	vk::raii::Queue & queue;

	// Scene format
	const vk::Extent2D output_size;
	const vk::Format output_format;
	const vk::Format depth_format;

	// Destination images
	struct output_image
	{
		vk::raii::Framebuffer framebuffer = nullptr;
		vk::raii::ImageView image_view = nullptr;

		image_allocation depth_buffer;
		vk::raii::ImageView depth_view = nullptr;

		image_allocation multisample_image;
		vk::raii::ImageView multisample_view = nullptr;
	};

	// Initialization functions
	output_image create_output_image_data(vk::Image output);
	vk::raii::RenderPass create_renderpass();
	vk::raii::PipelineLayout create_pipeline_layout(std::span<vk::DescriptorSetLayout> layouts);
	vk::raii::Pipeline create_pipeline(const pipeline_info & info);

	std::shared_ptr<scene_data::texture> create_default_texture(vk::raii::CommandPool & cb_pool, std::initializer_list<float> pixel);
	std::shared_ptr<scene_data::material> create_default_material(vk::raii::CommandPool & cb_pool);

	// Caches
	std::unordered_map<VkImage, output_image> output_images;
	std::unordered_map<pipeline_info, vk::raii::Pipeline> pipelines;

	output_image & get_output_image_data(vk::Image output);
	vk::raii::Pipeline & get_pipeline(const pipeline_info & info);

	// Descriptor set 0: per-frame/view data (uniform buffer) and per-instance data (dynamic uniform buffer)
	growable_descriptor_pool ds_pool_frame;
	vk::raii::DescriptorSetLayout descriptor_set_frame = nullptr;

	// Descriptor set 1: per-material data (5 combined image samplers and 1 uniform buffer)
	growable_descriptor_pool ds_pool_material;
	vk::raii::DescriptorSetLayout descriptor_set_material = nullptr;

	vk::raii::PipelineLayout pipeline_layout = nullptr;

	// Texture samplers
	std::unordered_map<sampler_info, std::shared_ptr<vk::raii::Sampler>> samplers;
	vk::Sampler get_sampler(const sampler_info & info);

	// Render pass
	vk::raii::RenderPass renderpass = nullptr;

	// Default material and textures
	// This material has 1x1 textures with the following values:
	//  base_color           (1.0, 1.0, 1.0, 1.0)
	//  metallic_roughness   (1.0, 1.0)
	//  occlusion            (1.0)
	//  emissive             (0.0, 0.0, 0.0)
	//  normal               (0.5, 0.5, 1.0)
	// And:
	//  base_color_factor    (1.0, 1.0, 1.0, 1.0)
	//  base_emissive_factor (0.0, 0.0, 0.0)
	//  metallic_factor      1.0
	//  roughness_factor     1.0
	//  occlusion_strength   0.0
	//  normal_scale         0.0
	std::shared_ptr<scene_data::material> default_material;

	struct frame_gpu_data
	{
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec4 light_position;
		glm::vec4 ambient_color;
		glm::vec4 light_color;
	};

	struct instance_gpu_data
	{
		glm::mat4 model;
		glm::mat4 modelview;
		glm::mat4 modelviewproj;
	};

	struct per_frame_resources
	{
		vk::raii::Fence fence = nullptr;
		vk::raii::CommandBuffer cb = nullptr;
		vk::raii::QueryPool query_pool = nullptr;
		std::vector<std::shared_ptr<void>> resources;

                // Uniform buffer for per-view and per-instance data
	        // host visible, host coherent
		size_t staging_buffer_offset;
   	        buffer_allocation staging_buffer;

	        // device local
	        // buffer_allocation uniform_buffer;

		std::chrono::steady_clock::time_point cpu_time_start;
	};

	std::vector<per_frame_resources> frame_resources;
	int current_frame_index;

	per_frame_resources& current_frame();

	void update_material_descriptor_set(scene_data::material& material);

public:
	scene_renderer(vk::raii::Device & device, vk::raii::PhysicalDevice physical_device, vk::raii::Queue & queue, vk::raii::CommandPool & cb_pool, vk::Extent2D output_size, vk::Format output_format, std::span<vk::Format> depth_formats, int frames_in_flight = 2);

	~scene_renderer();

	struct frame_info
	{
		vk::Image destination;
		glm::mat4 projection;
		glm::mat4 view;
	};

	void start_frame();
	void render(scene_data & scene, const std::array<float, 4>& clear_color, std::span<frame_info> info);
	void end_frame();

	std::shared_ptr<scene_data::material> get_default_material()
	{
		return default_material;
	}

	void wait_idle();
};
