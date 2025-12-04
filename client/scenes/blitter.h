/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "vk/allocation.h"
#include "wivrn_packets.h"
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{
class blitter
{
public:
	struct output
	{
		vk::ImageView rgb;
		vk::Sampler sampler_rgb;
		vk::Rect2D rect_rgb;
		vk::ImageLayout layout_rgb;
		vk::ImageView a;
		vk::Sampler sampler_a;
		vk::Rect2D rect_a;
		vk::ImageLayout layout_a;
	};

private:
	vk::raii::Device & device;
	size_t view;
	to_headset::video_stream_description desc;
	// indices to simply passthrough, if -1 image needs to be blitted
	int passthrough_rgb = -1;
	int passthrough_a = -1;
	image_allocation target;
	vk::raii::ImageView image_view = nullptr;
	vk::raii::RenderPass rp = nullptr;
	vk::raii::Framebuffer fb = nullptr;
	vk::raii::Sampler sampler = nullptr;
	struct pipeline_t
	{
		vk::raii::DescriptorSetLayout set_layout = nullptr;
		vk::raii::PipelineLayout layout = nullptr;
		vk::raii::Pipeline pipeline = nullptr;
		bool used = false; // true if we need this image for blitting
	};
	std::vector<pipeline_t> pipelines;
	output current;

public:
	blitter(vk::raii::Device & device, size_t view);

	void reset(const to_headset::video_stream_description & desc);
	void begin(vk::raii::CommandBuffer &);
	// Return true if the image is used for this blitter's rgb
	bool push_image(vk::raii::CommandBuffer & buf, uint8_t stream, vk::Sampler sampler, const vk::Extent2D &, vk::ImageView image, vk::ImageLayout);
	output end(vk::raii::CommandBuffer &);
};
} // namespace wivrn
