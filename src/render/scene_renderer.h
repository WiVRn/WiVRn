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

#include <memory>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

#include "tiny_gltf.h"
#include "vk/buffer.h"
#include "vk/command_pool.h"
#include "vk/device_memory.h"
#include "vk/image.h"
#include "vk/pipeline.h"
#include "vk/renderpass.h"

class scene_renderer
{
public:
	class shader
	{
		friend class scene_renderer;

		VkDescriptorSetLayout descriptor_set_layout{};
		std::vector<VkDescriptorPool> descriptor_pools;
		VkPipelineLayout pipeline_layout{};
		VkPipeline pipeline{};
		VkShaderModule vertex_shader;
		VkShaderModule fragment_shader;
	};

	class image
	{
		friend class scene_renderer;

		VkDeviceMemory memory{};
		VkSampler sampler{};
		VkImage image{};
		VkImageView image_view{};
		VkDescriptorSet descriptor_set{};
	};

	class buffer
	{
		friend class scene_renderer;

		VkDeviceMemory memory{};
		VkBuffer buffer{};
	};

	struct mesh_primitive
	{
		shader * s;
	};

	class model
	{
		friend class scene_renderer;

		tinygltf::Model gltf_model;
		std::vector<image *> images;
		std::vector<buffer *> buffers;
	};

	class model_instance
	{
		friend class scene_renderer;

		model * gltf_model;

		void * ubo;
	};

private:
	VkDevice device;
	VkPhysicalDevice physical_device;
	VkPhysicalDeviceProperties physical_device_properties;
	VkQueue queue;
	vk::command_pool command_pool;

	tinygltf::TinyGLTF gltf_loader;

	// Staging buffer
	size_t staging_buffer_size = 0;
	vk::buffer staging_buffer;
	vk::device_memory staging_memory;
	VkFence staging_fence{};
	void reserve(size_t size);

	void load_buffer(VkBuffer b, const void * data, size_t size);

	template <typename T>
	void load_buffer(VkBuffer b, std::span<T> data)
	{
		load_buffer(b, data.data(), data.size() * sizeof(T));
	}

	void load_image(VkImage i, void * data, VkExtent2D size, VkFormat format, uint32_t mipmap_count, VkImageLayout final_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Scene data
	std::vector<std::unique_ptr<shader>> shaders;
	std::vector<std::unique_ptr<image>> images;
	std::vector<std::unique_ptr<buffer>> buffers;
	std::vector<std::unique_ptr<mesh_primitive>> primitives;
	std::vector<std::unique_ptr<model>> models;
	std::vector<model_instance> instances;

	// Destination images
	std::vector<VkImage> output_images;
	std::vector<VkImageView> output_image_views;
	std::vector<VkFramebuffer> output_framebuffers;
	VkExtent2D output_size{};
	VkFormat output_format{};

	// Render pass
	vk::renderpass renderpass;

	void cleanup();
	void cleanup_output_images();
	void cleanup_shader(shader &);
	void cleanup_buffer(buffer &);
	void cleanup_image(image &);

public:
	scene_renderer(VkDevice device, VkPhysicalDevice physical_device, VkQueue queue);
	~scene_renderer();
	scene_renderer(const scene_renderer &) = delete;
	scene_renderer(scene_renderer &&) = delete;

	void set_output_images(std::vector<VkImage> output_images, VkExtent2D output_size, VkFormat output_format);

	shader * create_shader(std::string name, VkPrimitiveTopology topology, std::vector<VkDescriptorSetLayoutBinding> uniform_bindings, std::vector<VkVertexInputBindingDescription> vertex_bindings, std::vector<VkVertexInputAttributeDescription> vertex_attributes);

	image * create_image(void * data, VkExtent2D size, VkFormat format);

	buffer * create_buffer(const void * data, size_t size, VkBufferUsageFlags usage);

	template <typename T>
	// requires(std::contiguous_iterator<typename T::iterator>)
	buffer * create_buffer(const T & data,
	                       VkBufferUsageFlags usage)
	{
		return create_buffer(data.data(), data.size() * sizeof(T), usage);
	}

	model * load_gltf(const std::string & filename);
};
