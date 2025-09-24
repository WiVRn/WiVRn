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

#include "wivrn_packets.h"

#include <cstdint>
#include <memory>
#include <span>
#include <vulkan/vulkan_raii.hpp>

namespace scenes
{
class stream;
}

namespace wivrn
{
class shard_accumulator;
class decoder
{
public:
	struct blit_handle
	{
		wivrn::from_headset::feedback feedback;
		wivrn::to_headset::video_stream_data_shard::view_info_t view_info;
		vk::ImageView image_view = nullptr;
		vk::Image image = nullptr;
		vk::ImageLayout & current_layout;
		vk::Semaphore semaphore = nullptr;
		uint64_t * semaphore_val = nullptr;
	};

	const wivrn::to_headset::video_stream_description::item description;

protected:
	decoder(const wivrn::to_headset::video_stream_description::item & description) :
	        description(description) {}
	vk::Extent2D extent_; // Must be populated when sampler is set

public:
	static std::shared_ptr<decoder> make(
	        vk::raii::Device &,
	        vk::raii::PhysicalDevice &,
	        uint32_t vk_queue_family_index,
	        const wivrn::to_headset::video_stream_description::item & description,
	        float fps,
	        uint8_t stream_index,
	        std::weak_ptr<scenes::stream>,
	        shard_accumulator *);
	virtual ~decoder();
	virtual void push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial) = 0;

	virtual void frame_completed(
	        const from_headset::feedback & feedback,
	        const to_headset::video_stream_data_shard::view_info_t & view_info) = 0;

	virtual vk::Sampler sampler() = 0;
	const vk::Extent2D extent()
	{
		return extent_;
	}

	static std::vector<wivrn::video_codec> supported_codecs();
};
} // namespace wivrn
