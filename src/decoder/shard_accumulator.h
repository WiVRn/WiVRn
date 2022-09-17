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

#include "xr/instance.h"
#include <memory>
#ifdef XR_USE_PLATFORM_ANDROID
#include "decoder/android/android_decoder.h"
using decoder_impl = ::wivrn::android::decoder;
#else
#include "decoder/ffmpeg/ffmpeg_decoder.h"
using decoder_impl = ::ffmpeg::decoder;
#endif

#include "wivrn_packets.h"
#include <array>
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

class shard_accumulator
{
	std::shared_ptr<decoder_impl> decoder;

public:
	using data_shard = xrt::drivers::wivrn::to_headset::video_stream_data_shard;
	using parity_shard = xrt::drivers::wivrn::to_headset::video_stream_parity_shard;
	struct shard_set
	{
		size_t num_shards = 0;
		size_t min_for_reconstruction = -1;
		std::vector<std::optional<data_shard>> data;
		std::vector<std::optional<parity_shard>> parity;
		void reset(uint64_t frame_index);
		bool empty() const;

		uint16_t insert(data_shard &&);
		std::optional<uint16_t> insert(parity_shard &&);

		parity_shard & get_parity();
		std::optional<uint16_t> try_reconstruct();

		xrt::drivers::wivrn::from_headset::feedback feedback{};

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

public:
	explicit shard_accumulator(
	        VkDevice device,
	        VkPhysicalDevice physical_device,
	        const xrt::drivers::wivrn::to_headset::video_stream_description::item & description,
	        std::weak_ptr<scenes::stream> scene,
	        uint8_t stream_index) :
	        decoder(std::make_shared<decoder_impl>(device, physical_device, description, scene, this)),
	        current(stream_index),
	        next(stream_index),
	        weak_scene(scene)
	{
		next.reset(1);
	}

	template <typename Shard>
	void push_shard(Shard &&);

	auto & desc() const
	{
		return decoder->desc();
	}

	using blit_handle = decoder_impl::blit_handle;
	using blit_target = decoder_impl::blit_target;

	static const VkImageLayout framebuffer_expected_layout = decoder_impl::framebuffer_expected_layout;
	static const VkImageUsageFlagBits framebuffer_usage = decoder_impl::framebuffer_usage;

	void set_blit_targets(std::vector<blit_target> targets, VkFormat format)
	{
		decoder->set_blit_targets(std::move(targets), format);
	}

	void blit(VkCommandBuffer command_buffer, blit_handle & handle, std::span<int> blit_target_indices)
	{
		return decoder->blit(command_buffer, handle, blit_target_indices);
	}

private:
	void try_submit_frame(std::optional<uint16_t> shard_idx);
	void try_submit_frame(uint16_t shard_idx);
	void send_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback);
	void advance();
};
