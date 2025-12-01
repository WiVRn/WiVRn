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

#include "render/vertex_layout.h"
#include "utils/cache.h"
#include "vk/shader.h"
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

#include "render/scene_components.h"
#include "utils/thread_safe.h"

struct renderpass_info
{
	vk::Format color_format;
	vk::Format depth_format;
	bool keep_depth_buffer;
	vk::SampleCountFlagBits msaa_samples = vk::SampleCountFlagBits::e1;
	bool fragment_density_map;
	uint32_t multiview_count;

	bool operator==(const renderpass_info & other) const noexcept = default;
};

struct pipeline_info
{
	renderpass_info renderpass;

	std::string vertex_shader_name;
	std::string fragment_shader_name;
	renderer::vertex_layout vertex_layout;

	vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eNone;
	vk::FrontFace front_face = vk::FrontFace::eClockwise;
	vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
	bool blend_enable = false;
	bool depth_test_enable = true;
	bool depth_write_enable = true;

	// Specialization constants data
	int32_t nb_texcoords = 2;
	VkBool32 dithering = true;
	VkBool32 alpha_cutout = false;
	VkBool32 skinning = false;

	bool operator==(const pipeline_info & other) const noexcept = default;
};

struct output_image_info
{
	renderpass_info renderpass;

	vk::Extent2D output_size;
	VkImage color;
	VkImage depth;
	VkImage foveation;

	bool operator==(const output_image_info & other) const noexcept = default;
};

namespace std
{
template <>
struct hash<renderpass_info> : utils::magic_hash<renderpass_info>
{};

template <>
struct hash<pipeline_info> : utils::magic_hash<pipeline_info>
{};

template <>
struct hash<vk::Extent2D> : utils::magic_hash<vk::Extent2D>
{};

template <>
struct hash<output_image_info> : utils::magic_hash<output_image_info>
{};
} // namespace std

struct image_loader;

class scene_renderer
{
	vk::raii::PhysicalDevice physical_device;
	vk::raii::Device & device;
	vk::PhysicalDeviceProperties physical_device_properties;
	thread_safe<vk::raii::Queue> & queue;
	uint32_t queue_family_index;
	vk::raii::CommandPool cb_pool;

	// Destination images
	struct output_image
	{
		vk::raii::Framebuffer framebuffer = nullptr;
		vk::raii::ImageView image_view = nullptr;

		image_allocation depth_buffer;
		vk::raii::ImageView depth_view = nullptr;

		image_allocation multisample_image;
		vk::raii::ImageView multisample_view = nullptr;

		vk::raii::ImageView foveation_view = nullptr;
	};

	struct renderpass
	{
		vk::raii::RenderPass renderpass = nullptr;
		vk::AttachmentReference color_attachment;
		vk::AttachmentReference depth_attachment;
		std::optional<vk::AttachmentReference> resolve_attachment;          // Only used if MSAA is enabled
		std::optional<vk::AttachmentReference> fragment_density_attachment; // Only used if fragment density map is enabled
		int attachment_count;
	};

	// Initialization functions
	output_image create_output_image_data(const output_image_info & info);
	renderpass create_renderpass(const renderpass_info & info);
	vk::raii::PipelineLayout create_pipeline_layout(std::span<vk::DescriptorSetLayout> layouts);
	vk::raii::Pipeline create_pipeline(const pipeline_info & info);

	std::shared_ptr<renderer::texture> create_default_texture(image_loader & loader, std::vector<uint8_t> pixel, const std::string & name);
	std::shared_ptr<renderer::material> create_default_material();
	vk::raii::DescriptorSetLayout create_descriptor_set_layout(std::span<vk::DescriptorSetLayoutBinding> bindings, vk::DescriptorSetLayoutCreateFlags flags = {});

	// Caches
	std::unordered_map<renderpass_info, renderpass> renderpasses;
	std::unordered_map<output_image_info, output_image> output_images;
	std::unordered_map<pipeline_info, vk::raii::Pipeline> pipelines;
	utils::cache<std::string, shader, shader_loader> shader_cache;

	output_image & get_output_image_data(const output_image_info & info);
	vk::raii::Pipeline & get_pipeline(const pipeline_info & info);
	renderpass & get_renderpass(const renderpass_info & info);

	vk::raii::DescriptorSetLayout layout_0; // Descriptor set 0: per-frame/view data (UBO), per-instance data (UBO + SSBO), per-material data (5 combined image samplers and 1 uniform buffer)

	vk::raii::PipelineLayout pipeline_layout = nullptr;

	// Texture samplers
	std::unordered_map<renderer::sampler_info, std::shared_ptr<vk::raii::Sampler>> samplers;
	vk::Sampler get_sampler(const renderer::sampler_info & info);

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
	//  alpha_cutoff         0.5
	std::shared_ptr<renderer::material> default_material;

	struct frame_gpu_data
	{
		std::array<glm::mat4, 2> view;
		glm::vec4 light_position;
		glm::vec4 ambient_color;
		glm::vec4 light_color;
	};

	struct instance_gpu_data
	{
		glm::mat4 model;
		std::array<glm::mat4, 2> modelview;
		std::array<glm::mat4, 2> modelviewproj;
		std::array<glm::vec4, 4> clipping_planes;
	};

	struct debug_draw_vertex
	{
		glm::vec4 position;
		glm::vec4 colour;
	};

	std::vector<debug_draw_vertex> debug_draw_vertices;

public:
	struct stats
	{
		size_t count_primitives;
		size_t count_culled_primitives;
		size_t count_triangles;
		size_t count_culled_triangles;
	};

private:
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

		buffer_allocation debug_draw;

		// Statistics
		stats frame_stats;
	};

	std::vector<per_frame_resources> frame_resources;
	int current_frame_index;
	vk::raii::QueryPool query_pool = nullptr;
	double gpu_time_s = 0;

	per_frame_resources & current_frame();

public:
	static vk::Format find_usable_image_format(
	        vk::raii::PhysicalDevice physical_device,
	        std::span<const vk::Format> formats,
	        vk::Extent3D min_extent,
	        vk::ImageUsageFlags usage,
	        vk::ImageType type = vk::ImageType::e2D,
	        vk::ImageTiling tiling = vk::ImageTiling::eOptimal,
	        vk::ImageCreateFlags flags = {});

	scene_renderer(
	        vk::raii::Device & device,
	        vk::raii::PhysicalDevice physical_device,
	        thread_safe<vk::raii::Queue> & queue,
	        uint32_t queue_family_index,
	        int frames_in_flight = 2);

	~scene_renderer();

	struct frame_info
	{
		glm::mat4 projection;
		glm::mat4 view;
	};

	void start_frame();
	void render(
	        entt::registry & scene,
	        const std::array<float, 4> & clear_color,
	        uint32_t layer_mask,
	        vk::Extent2D output_size,
	        vk::Format output_format,
	        vk::Format depth_format,
	        vk::Image color_buffer,
	        vk::Image depth_buffer,
	        vk::Image foveation_image,
	        std::span<frame_info> info,
	        bool render_debug_draws = false);
	void end_frame();

	void debug_draw_clear();
	// void debug_draw(std::span<glm::vec3> line, glm::vec4 color);
	void debug_draw_box(const glm::mat4 & model, glm::vec3 min, glm::vec3 max, glm::vec4 color);

	const stats & last_frame_stats() const
	{
		return frame_resources[current_frame_index].frame_stats;
	}

	double get_gpu_time() const
	{
		return gpu_time_s;
	}

	std::shared_ptr<renderer::material> get_default_material()
	{
		return default_material;
	}

	void wait_idle();
};
