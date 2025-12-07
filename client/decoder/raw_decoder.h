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

#include "decoder.h"

#include "vk/allocation.h"

#include <atomic>

namespace wivrn
{
class raw_decoder : public decoder
{
private:
	static const int image_count = 5;
	struct image
	{
		image_allocation image;
		vk::raii::ImageView view_full = nullptr;
		vk::ImageLayout current_layout = vk::ImageLayout::eUndefined;
		std::atomic_bool free = true;
		vk::raii::Semaphore semaphore = nullptr;
		uint64_t semaphore_val = 0;
	};

	vk::raii::Device & device;
	vk::raii::SamplerYcbcrConversion ycbcr_conversion;
	vk::raii::Sampler sampler_;

	vk::raii::CommandPool command_pool;
	vk::CommandBuffer cmd;
	vk::raii::Fence fence;

	uint8_t stream_index;
	const vk::Extent2D extent;

	std::array<image, image_count> image_pool;

	std::weak_ptr<scenes::stream> weak_scene;
	shard_accumulator * accumulator;

	uint64_t current_frame = 0;
	std::array<buffer_allocation, 2> input;
	uint8_t * input_pos;
	from_headset::feedback feedback;

public:
	raw_decoder(vk::raii::Device & device,
	            vk::raii::PhysicalDevice & physical_device,
	            uint32_t vk_queue_family_index,
	            const wivrn::to_headset::video_stream_description & description,
	            uint8_t stream_index,
	            std::weak_ptr<scenes::stream> scene,
	            shard_accumulator * accumulator);

	void push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial) override;

	void frame_completed(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info) override;

	vk::Sampler sampler() override
	{
		return *sampler_;
	}

	static std::vector<wivrn::video_codec> supported_codecs();

private:
	image * get_free();
};

} // namespace wivrn
