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

#include "pyrowave/pyrowave_decoder.h"
#include "utils/thread_safe.h"
#include "vk/allocation.h"
#include "wivrn_packets.h"
#include <memory>
#include <span>
#include <thread>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{
class shard_accumulator;
}

namespace scenes
{
class stream;
}

namespace wivrn
{
class decoder
{
public:
	struct blit_handle
	{
		wivrn::from_headset::feedback feedback;
		wivrn::to_headset::video_stream_data_shard::view_info_t view_info;
		vk::raii::ImageView & image_view;
		vk::Image image = nullptr;
		vk::ImageLayout * current_layout = nullptr;
		vk::Semaphore semaphore = nullptr;
		uint64_t & semaphore_val;

		std::atomic_bool & free;

		~blit_handle();
	};

private:
	static const int image_count = 12;
	struct image
	{
		image_allocation image;
		vk::raii::ImageView view_full = nullptr;
		vk::raii::ImageView view_y = nullptr;
		vk::raii::ImageView view_cb = nullptr;
		vk::raii::ImageView view_cr = nullptr;
		vk::ImageLayout current_layout = vk::ImageLayout::eUndefined;
		std::atomic_bool free = true;
		vk::raii::Semaphore semaphore = nullptr;
		uint64_t semaphore_val = 0;
	};

	vk::raii::SamplerYcbcrConversion ycbcr_conversion;
	vk::raii::Sampler sampler_;

	std::array<image, image_count> image_pool;

	wivrn::to_headset::video_stream_description::item description;

	std::weak_ptr<scenes::stream> weak_scene;
	shard_accumulator * accumulator;

	PyroWave::Decoder dec;

	struct pending_data
	{
		std::unique_ptr<PyroWave::DecoderInput> input;
		from_headset::feedback feedback;
		to_headset::video_stream_data_shard::view_info_t view_info;
		bool ready = false;
	};
	thread_safe_notifyable<pending_data> pending;
	std::unique_ptr<PyroWave::DecoderInput> input_acc;
	std::thread worker;
	std::atomic_bool exiting = false;

public:
	decoder(vk::raii::Device & device,
	        vk::raii::PhysicalDevice & physical_device,
	        uint32_t vk_queue_family_index,
	        const wivrn::to_headset::video_stream_description::item & description,
	        float fps,
	        uint8_t stream_index,
	        std::weak_ptr<scenes::stream> scene,
	        shard_accumulator * accumulator);

	~decoder();

	void push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial);

	void frame_completed(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info);

	const auto & desc() const
	{
		return description;
	}

	vk::Sampler sampler()
	{
		return *sampler_;
	}

	vk::Extent2D image_size()
	{
		return {
		        description.width,
		        description.height,
		};
	}

	static std::vector<wivrn::video_codec> supported_codecs();

private:
	image * get_free();
	void worker_function(uint32_t vk_queue_family_index);
};
} // namespace wivrn
