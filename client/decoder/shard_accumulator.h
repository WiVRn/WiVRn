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

#include <memory>
#include <vulkan/vulkan.hpp>

#ifdef __ANDROID__
#include "decoder/android/android_decoder.h"
using decoder_impl = ::wivrn::android::decoder;
#else
#include "decoder/ffmpeg/ffmpeg_decoder.h"
using decoder_impl = ::wivrn::ffmpeg::decoder;
#endif

#include "wivrn_packets.h"
#include <optional>
#include <vector>

namespace xr
{
class instance;
}

namespace wivrn
{

class shard_accumulator
{
	std::shared_ptr<decoder_impl> decoder;

public:
	using data_shard = wivrn::to_headset::video_stream_data_shard;
	struct shard_set
	{
		size_t min_for_reconstruction = -1;
		std::vector<std::optional<data_shard>> data;
		void reset(uint64_t frame_index);
		bool empty() const;

		std::optional<uint16_t> insert(data_shard &&, xr::instance & instance);

		wivrn::from_headset::feedback feedback{};

		explicit shard_set(uint8_t stream_index);
		shard_set(const shard_set &) = default;
		shard_set(shard_set &&) = default;
		shard_set & operator=(const shard_set &) = default;
		shard_set & operator=(shard_set &&) = default;

		uint64_t frame_index() const
		{
			return feedback.frame_index;
		}
	};

private:
	shard_set current;
	shard_set next;
	std::weak_ptr<scenes::stream> weak_scene;
	xr::instance & instance;

public:
	explicit shard_accumulator(
	        vk::raii::Device & device,
	        vk::raii::PhysicalDevice & physical_device,
	        xr::instance & instance,
	        const wivrn::to_headset::video_stream_description::item & description,
	        float fps,
	        std::weak_ptr<scenes::stream> scene,
	        uint8_t stream_index) :
	        decoder(std::make_shared<decoder_impl>(device, physical_device, description, fps, stream_index, scene, this)),
	        current(stream_index),
	        next(stream_index),
	        weak_scene(scene),
	        instance(instance)
	{
		next.reset(1);
	}

	void push_shard(wivrn::to_headset::video_stream_data_shard &&);

	auto & desc() const
	{
		return decoder->desc();
	}

	vk::Sampler sampler()
	{
		return decoder->sampler();
	}

	vk::Extent2D image_size()
	{
		return decoder->image_size();
	}

	using blit_handle = decoder_impl::blit_handle;

private:
	void try_submit_frame(std::optional<uint16_t> shard_idx);
	void try_submit_frame(uint16_t shard_idx);
	void send_feedback(wivrn::from_headset::feedback & feedback);
	void advance();
};
} // namespace wivrn
