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
#include "vk/buffer.h"
#include "vk/image.h"
#include "vk/renderpass.h"
#include "wivrn_client.h"
#include "wivrn_discover.h"
#include <tiny_gltf.h>

#include "render/scene_renderer.h"
#include "render/text_rasterizer.h"

namespace scenes
{
class stream;

class lobby : public scene
{
	std::string status_string;
	std::string last_status_string;
	VkSampler status_string_sampler{};
	text status_string_rasterized_text;
	VkImageView status_string_image_view{};
	VkDescriptorPool status_string_descriptor_pool{};
	VkDescriptorSetLayout status_string_image_descriptor_set_layout{};
	VkDescriptorSet status_string_image_descriptor_set{};
	text_rasterizer status_string_rasterizer;

	vk::renderpass renderpass;
	vk::pipeline_layout layout;
	vk::pipeline pipeline;

	VkFence fence{};
	VkCommandBuffer command_buffer{};

	struct image_data
	{
		VkFramebuffer framebuffer{};
		VkSemaphore render_finished{};
	};

	std::vector<std::vector<image_data>> images_data;

	wivrn_discover discover;

	tinygltf::Model model;
	std::vector<vk::device_memory> model_memory;
	std::vector<vk::buffer> model_buffers;
	std::vector<vk::image> model_images;

	void load_model(const std::string & filename);

	std::shared_ptr<stream> next_scene;

	scene_renderer renderer;

	void rasterize_status_string();

public:
	virtual ~lobby();
	lobby();

	virtual void render() override;
	virtual void render_view(XrViewStateFlags flags, XrTime display_time, XrView & view, int swapchain_index, int image_index) override;
};
} // namespace scenes
