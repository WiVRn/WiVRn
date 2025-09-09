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

#include "decoder/decoder.h"
#include "vk/allocation.h"
#include "wivrn_packets.h"
#include <memory>
#include <mutex>
#include <span>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{
class shard_accumulator;
}

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
class decoder : public wivrn::decoder
{
private:
	struct ffmpeg_blit_handle;
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
	std::vector<int> free_images;

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
	        uint8_t stream_index,
	        std::weak_ptr<scenes::stream> scene,
	        shard_accumulator * accumulator);

	void push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial) override;

	void frame_completed(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info) override;

	vk::Sampler sampler() override
	{
		return *rgb_sampler;
	}

	static void supported_codecs(std::vector<wivrn::video_codec> &);
};
} // namespace wivrn::ffmpeg
