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

#include <array>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

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

	// Specialization constants data
	int32_t nb_texcoords = 2;
	int32_t nb_clipping = 1;
	VkBool32 dithering = true;
	VkBool32 alpha_cutout = false;
	VkBool32 skinning = false;

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
	output_image create_output_image_data(vk::Image output_color, vk::Image output_depth);
	vk::raii::RenderPass create_renderpass();
	vk::raii::PipelineLayout create_pipeline_layout(std::span<vk::DescriptorSetLayout> layouts);
	vk::raii::Pipeline create_pipeline(const pipeline_info & info);

	std::shared_ptr<scene_data::texture> create_default_texture(vk::raii::CommandPool & cb_pool, std::vector<uint8_t> pixel);
	std::shared_ptr<scene_data::material> create_default_material(vk::raii::CommandPool & cb_pool);
	vk::raii::DescriptorSetLayout create_descriptor_set_layout(std::span<vk::DescriptorSetLayoutBinding> bindings, vk::DescriptorSetLayoutCreateFlags flags = {});

	// Caches
	std::unordered_map<VkImage, output_image> output_images;
	std::unordered_map<pipeline_info, vk::raii::Pipeline> pipelines;

	output_image & get_output_image_data(vk::Image output_color, vk::Image output_depth);
	vk::raii::Pipeline & get_pipeline(const pipeline_info & info);

	vk::raii::DescriptorSetLayout layout_0; // Descriptor set 0: per-frame/view data (UBO) and per-instance data (UBO + SSBO)
	vk::raii::DescriptorSetLayout layout_1; // Descriptor set 1: per-material data (5 combined image samplers and 1 uniform buffer)

	// Descriptor set 1: per-material data (5 combined image samplers and 1 uniform buffer)
	growable_descriptor_pool ds_pool_material;

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
		std::vector<std::shared_ptr<void>> resources;
		bool query_pool_filled = false;

		// Buffer for per-view and per-instance data
		// device local, host visible, host coherent
		size_t uniform_buffer_offset;
		buffer_allocation uniform_buffer;
	};

	std::vector<per_frame_resources> frame_resources;
	int current_frame_index;
	vk::raii::QueryPool query_pool = nullptr;
	double gpu_time_s = 0;
	bool keep_depth_buffer = false;

	per_frame_resources & current_frame();

	void update_material_descriptor_set(scene_data::material & material);

public:
	static vk::Format find_usable_image_format(
	        vk::raii::PhysicalDevice physical_device,
	        std::span<vk::Format> formats,
	        vk::Extent3D min_extent,
	        vk::ImageUsageFlags usage,
	        vk::ImageType type = vk::ImageType::e2D,
	        vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
	        vk::ImageCreateFlags flags = {});

	scene_renderer(
	        vk::raii::Device & device,
	        vk::raii::PhysicalDevice physical_device,
	        vk::raii::Queue & queue,
	        vk::raii::CommandPool & cb_pool,
	        vk::Extent2D output_size,
	        vk::Format output_format,
	        vk::Format depth_format,
	        int frames_in_flight = 2,
	        bool keep_depth_buffer = false);

	~scene_renderer();

	struct frame_info
	{
		vk::Image destination;
		vk::Image depth_buffer;
		glm::mat4 projection;
		glm::mat4 view;
	};

	void start_frame();
	void render(scene_data & scene, const std::array<float, 4> & clear_color, std::span<frame_info> info);
	void end_frame();

	double get_gpu_time() const
	{
		return gpu_time_s;
	}

	std::shared_ptr<scene_data::material> get_default_material()
	{
		return default_material;
	}

	void wait_idle();
};
