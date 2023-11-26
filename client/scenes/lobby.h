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

#include "scene.h"
#include <vulkan/vulkan_raii.hpp>
#include "wivrn_discover.h"

#include <optional>

#include "render/scene_renderer.h"
#include "render/text_rasterizer.h"

namespace scenes
{
class stream;

class lobby : public scene
{
	std::string status_string;
	std::string last_status_string;
	vk::raii::Sampler status_string_sampler = nullptr;
	text status_string_rasterized_text;
	vk::raii::ImageView status_string_image_view = nullptr;
	vk::raii::DescriptorPool status_string_descriptor_pool = nullptr;
	vk::raii::DescriptorSetLayout status_string_image_descriptor_set_layout = nullptr;
	vk::DescriptorSet status_string_image_descriptor_set;
	text_rasterizer status_string_rasterizer;

	vk::raii::RenderPass renderpass = nullptr;
	vk::raii::PipelineLayout layout = nullptr;
	vk::raii::Pipeline pipeline = nullptr;

	vk::raii::Fence fence = nullptr;
	vk::raii::CommandBuffer command_buffer = nullptr;

	struct image_data
	{
		vk::raii::Framebuffer framebuffer;
		vk::raii::Semaphore render_finished;
	};

	std::vector<std::vector<image_data>> images_data;

	std::optional<wivrn_discover> discover;

	std::vector<vk::DeviceMemory> model_memory;
	std::vector<vk::Buffer> model_buffers;
	std::vector<vk::Image> model_images;

	void load_model(const std::string & filename);

	std::shared_ptr<stream> next_scene;

	// scene_renderer renderer;

	void rasterize_status_string();

public:
	virtual ~lobby();
	lobby();

	void render() override;
	void render_view(XrViewStateFlags flags, XrTime display_time, XrView & view, int swapchain_index, int image_index);

	void on_unfocused() override;
	void on_focused() override;
};
} // namespace scenes
