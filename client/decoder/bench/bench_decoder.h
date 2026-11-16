/*
 * WiVRn VR streaming
 * Copyright (C) 2026  WiVRn Contributors
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

#include <cstdint>
#include <vector>

namespace scenes
{
class stream;
}

namespace wivrn
{
class shard_accumulator;
}

// Stands in for a real decoder when benchmarking the server's encoder: accepts real h264/h265/av1
// bitstreams (so stream negotiation and the encoder under test are unaffected) but never decodes
// or colour-converts them, so this client is never the bottleneck being measured. Only built when
// WIVRN_USE_BENCH_DECODER is set — see docs/benchmarking.md.
namespace wivrn::bench
{
class decoder : public wivrn::decoder
{
private:
	vk::raii::Device & device;
	vk::raii::Sampler sampler_;

	image_allocation image;
	vk::raii::ImageView view = nullptr;
	vk::ImageLayout current_layout = vk::ImageLayout::eUndefined;
	vk::raii::Semaphore semaphore;
	uint64_t semaphore_val = 0;

	std::weak_ptr<scenes::stream> weak_scene;
	shard_accumulator * accumulator;

public:
	decoder(vk::raii::Device & device,
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

	static void supported_codecs(std::vector<wivrn::video_codec> &);
};
} // namespace wivrn::bench
