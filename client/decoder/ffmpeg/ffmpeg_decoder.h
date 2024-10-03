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

#include "vk/allocation.h"
#include "wivrn_packets.h"
#include <memory>
#include <mutex>
#include <span>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

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

namespace wivrn::ffmpeg
{
class decoder
{
public:
	struct blit_handle
	{
		wivrn::from_headset::feedback feedback;
		wivrn::to_headset::video_stream_data_shard::timing_info_t timing_info;
		wivrn::to_headset::video_stream_data_shard::view_info_t view_info;
		vk::raii::ImageView & image_view;
		vk::Image image = nullptr;
		vk::ImageLayout * current_layout = nullptr;

		int image_index;
		decoder * self;

		~blit_handle();
	};

private:
	static const int image_count = 12;
	struct image
	{
		image_allocation image;
		vk::SubresourceLayout layout;
		uint64_t frame_index;
		vk::raii::ImageView image_view = nullptr;
		vk::ImageLayout current_layout = vk::ImageLayout::eUndefined;
	};

	vk::raii::Device & device;
	vk::raii::Sampler rgb_sampler = nullptr;

	std::array<image, image_count> decoded_images;
	vk::Extent2D extent{};
	std::vector<int> free_images;

	wivrn::to_headset::video_stream_description::item description;

	std::unique_ptr<AVCodecContext, void (*)(AVCodecContext *)> codec;
	std::unique_ptr<SwsContext, void (*)(SwsContext *)> sws;
	std::vector<uint8_t> packet;
	uint64_t frame_index;
	std::weak_ptr<scenes::stream> weak_scene;
	shard_accumulator * accumulator;

	std::mutex mutex;

public:
	decoder(vk::raii::Device & device,
	        vk::raii::PhysicalDevice & physical_device,
	        const wivrn::to_headset::video_stream_description::item & description,
	        float fps,
	        uint8_t stream_index,
	        std::weak_ptr<scenes::stream> scene,
	        shard_accumulator * accumulator);

	decoder(const decoder &) = delete;
	decoder(decoder &&) = delete;

	void push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial);

	void frame_completed(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::timing_info_t & timing_info,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info);

	const auto & desc() const
	{
		return description;
	}

	vk::Sampler sampler()
	{
		return *rgb_sampler;
	}

	vk::Extent2D image_size()
	{
		return extent;
	}

	static std::vector<wivrn::video_codec> supported_codecs();
};
} // namespace ffmpeg
