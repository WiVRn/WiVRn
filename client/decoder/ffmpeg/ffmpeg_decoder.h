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

#include "vk/device_memory.h"
#include "vk/image.h"
#include "wivrn_packets.h"
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

class shard_accumulator;

extern "C"
{
	struct AVBufferRef;
	struct AVCodecContext;
	struct SwsContext;
}

namespace scenes
{
class stream;
}

namespace ffmpeg
{
class decoder
{
public:
	struct blit_target
	{
		VkImage image;
		VkImageView image_view;
		VkOffset2D offset;
		VkExtent2D extent;
	};

private:
	static const int image_count = 3;
	struct image
	{
		vk::image image;
		vk::device_memory memory;
		VkSubresourceLayout layout;
		uint64_t frame_index;
	};

	VkDevice device;
	std::array<image, image_count> decoded_images;
	std::vector<int> free_images;

	xrt::drivers::wivrn::to_headset::video_stream_description::item description;

	std::unique_ptr<AVCodecContext, void (*)(AVCodecContext *)> codec;
	std::unique_ptr<SwsContext, void (*)(SwsContext *)> sws;
	std::vector<uint8_t> packet;
	uint64_t frame_index;
	std::weak_ptr<scenes::stream> weak_scene;
	shard_accumulator * accumulator;

	std::vector<blit_target> blit_targets;
	std::mutex mutex;

public:
	decoder(
	        VkDevice device,
	        VkPhysicalDevice physical_device,
	        const xrt::drivers::wivrn::to_headset::video_stream_description::item & description,
		float fps,
	        std::weak_ptr<scenes::stream> scene,
	        shard_accumulator * accumulator);

	void push_data(std::span<uint8_t> data, uint64_t frame_index, bool partial);

	void frame_completed(const xrt::drivers::wivrn::from_headset::feedback &, const xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t & view_info);

	const auto & desc() const
	{
		return description;
	}

	struct blit_handle
	{
		xrt::drivers::wivrn::from_headset::feedback feedback;
		xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t view_info;
		int image_index;
		VkImage image;
		decoder * self;

		~blit_handle();
	};

	static const VkImageLayout framebuffer_expected_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	static const VkImageUsageFlagBits framebuffer_usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	void set_blit_targets(std::vector<blit_target> targets, VkFormat format);
	void blit(VkCommandBuffer command_buffer, blit_handle & handle, std::span<int> target_indices);
};
} // namespace ffmpeg
