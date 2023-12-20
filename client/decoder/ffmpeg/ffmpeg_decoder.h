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

#include "wivrn_packets.h"
#include <deque>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include "vk/allocation.h"

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
		vk::Image image;
		vk::ImageView image_view;
		vk::Offset2D offset;
		vk::Extent2D extent;
	};

private:
	static const int image_count = 3;
	struct image
	{
		image_allocation image;
		vk::SubresourceLayout layout;
		uint64_t frame_index;
	};

	vk::raii::Device& device;
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
	        vk::raii::Device& device,
	        vk::raii::PhysicalDevice& physical_device,
	        const xrt::drivers::wivrn::to_headset::video_stream_description::item & description,
	        float fps,
	        uint8_t stream_index,
	        std::weak_ptr<scenes::stream> scene,
	        shard_accumulator * accumulator);

	void push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial);

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

	static const vk::ImageLayout framebuffer_expected_layout = vk::ImageLayout::eTransferDstOptimal;
	static const vk::ImageUsageFlagBits framebuffer_usage = vk::ImageUsageFlagBits::eTransferDst;

	void set_blit_targets(std::vector<blit_target> targets, vk::Format format);
	void blit(vk::raii::CommandBuffer& command_buffer, blit_handle & handle, std::span<int> target_indices);
};
} // namespace ffmpeg
