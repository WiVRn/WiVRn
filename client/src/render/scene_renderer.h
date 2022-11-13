#pragma once

#include <memory>
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

#include "glm/vec2.hpp"
#include "glm/vec3.hpp"
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

		std::vector<VkDescriptorSetLayout> descriptor_set_layouts;
		std::vector<VkDescriptorPool> descriptor_pools;
		VkShaderModule vertex_shader{};
		VkShaderModule fragment_shader{};
		VkPipelineLayout pipeline_layout{};

		struct pipeline_info
		{
			VkPrimitiveTopology topology;
			std::vector<VkVertexInputBindingDescription> vertex_bindings;
			std::vector<VkVertexInputAttributeDescription> vertex_attributes;
		};

		std::vector<std::pair<pipeline_info, VkPipeline>> pipelines;

		shader() = default;
		shader(const shader &) = delete;
		shader(shader &&) = delete;
	};

	class image
	{
		friend class scene_renderer;

		VkDeviceMemory memory{};
		VkSampler sampler{};
		VkImage vk_image{};
		VkImageView image_view{};
		VkDescriptorSet descriptor_set{};

		image() = default;
		image(const image &) = delete;
		image(image &&) = delete;
	};

	class buffer
	{
		friend class scene_renderer;

		VkDeviceMemory memory{};
		VkBuffer vk_buffer{};

		buffer() = default;
		buffer(const buffer &) = delete;
		buffer(buffer &&) = delete;
	};

	struct mesh_primitive
	{
		struct vertex
		{
			glm::vec3 position;
			glm::vec3 normal;
			glm::vec2 texcoord;
		};

		VkPrimitiveTopology topology;

		VkPipeline pipeline;

		std::weak_ptr<buffer> indices;
		size_t indices_offset;
		size_t indices_count;

		std::weak_ptr<buffer> vertices;
		size_t vertices_offset;
	};

	class model
	{
		friend class scene_renderer;

		tinygltf::Model gltf_model;
		std::vector<std::weak_ptr<image>> images;
		std::vector<std::weak_ptr<buffer>> buffers;
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
	std::vector<std::shared_ptr<shader>> shaders;
	std::vector<std::shared_ptr<image>> images;
	std::vector<std::shared_ptr<buffer>> buffers;
	std::vector<std::shared_ptr<mesh_primitive>> primitives;
	std::vector<std::shared_ptr<model>> models;
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
	void cleanup_shader(shader *);
	void cleanup_buffer(buffer *);
	void cleanup_image(image *);

public:
	scene_renderer(VkDevice device, VkPhysicalDevice physical_device, VkQueue queue);
	~scene_renderer();
	scene_renderer(const scene_renderer &) = delete;
	scene_renderer(scene_renderer &&) = delete;

	void set_output_images(std::vector<VkImage> output_images, VkExtent2D output_size, VkFormat output_format);

	std::weak_ptr<shader> create_shader(std::string name,
	                                    std::vector<std::vector<VkDescriptorSetLayoutBinding>> uniform_bindings);

	VkPipeline get_shader_pipeline(std::weak_ptr<shader> shader, VkPrimitiveTopology topology, std::span<VkVertexInputBindingDescription> vertex_bindings, std::span<VkVertexInputAttributeDescription> vertex_attributes);

	std::weak_ptr<image> create_image(void * data, VkExtent2D size, VkFormat format);

	std::weak_ptr<buffer> create_buffer(const void * data, size_t size, VkBufferUsageFlags usage);

	template <typename T>
	// requires(std::contiguous_iterator<typename T::iterator>)
	std::weak_ptr<buffer> create_buffer(const T & data, VkBufferUsageFlags usage)
	{
		return create_buffer(data.data(), data.size() * sizeof(T), usage);
	}

	model * load_gltf(const std::string & filename);
};
