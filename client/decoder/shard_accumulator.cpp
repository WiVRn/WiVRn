/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "shard_accumulator.h"
#include "application.h"
#include "scenes/stream.h"
#include "spdlog/spdlog.h"

using namespace xrt::drivers::wivrn::to_headset;
using shard_set = shard_accumulator::shard_set;
using data_shard = shard_accumulator::data_shard;

shard_set::shard_set(uint8_t stream_index)
{
	feedback.stream_index = stream_index;
}

void shard_set::reset(uint64_t frame_index)
{
	num_shards = 0;
	min_for_reconstruction = -1;
	data.clear();

	uint8_t stream_index = feedback.stream_index;
	feedback = {};
	feedback.frame_index = frame_index;
	feedback.stream_index = stream_index;
}

bool shard_set::empty() const
{
	return num_shards == 0;
}

static bool is_complete(const shard_set & shards)
{
	const auto & frame = shards.data;
	if (frame.empty())
		return false;
	if (not(frame.back() and frame.back()->flags & video_stream_data_shard::end_of_frame))
		return false;
	for (const auto & shard: frame)
		if (not shard)
			return false;
	return true;
}

uint16_t shard_set::insert(data_shard && shard)
{
	XrTime now = application::now();
	if (empty())
		feedback.received_first_packet = now;
	feedback.received_last_packet = now;

	auto idx = shard.shard_idx;
	if (idx >= data.size())
		data.resize(idx + 1);
	if (not data[idx])
		++num_shards;
	data[idx] = std::move(shard);
	return idx;
}

static void debug_why_not_sent(const shard_set & shards)
{
	const auto & frame = shards.data;
	if (frame.empty())
	{
		spdlog::info("frame {} was not sent because no shard was received", shards.frame_index());
		return;
	}
	int frame_idx = -1;
	size_t data = 0;
	for (const auto & shard: frame)
	{
		if (shard)
		{
			frame_idx = shard->frame_idx;
			++data;
		}
	}

	spdlog::info("frame {} was not sent with {} data shards", frame_idx, data);
}

void shard_accumulator::advance()
{
	std::swap(current, next);
	next.reset(current.frame_index() + 1);
}

void shard_accumulator::push_shard(video_stream_data_shard && shard)
{
	assert(current.frame_index() + 1 == next.frame_index());

	uint8_t frame_diff = shard.frame_idx - current.frame_index();
	if (shard.frame_idx < current.frame_index())
	{
		// frame is in the past, drop it
	}
	else if (frame_diff == 0)
	{
		auto shard_idx = current.insert(std::move(shard));
		try_submit_frame(shard_idx);
	}
	else if (frame_diff == 1)
	{
		next.insert(std::move(shard));
		if (is_complete(next))
		{
			debug_why_not_sent(current);
			send_feedback(current.feedback);

			advance();

			try_submit_frame(0);
		}
	}
	else if (frame_diff == 2)
	{
		debug_why_not_sent(current);
		send_feedback(current.feedback);

		advance();

		push_shard(std::move(shard));
	}
	else
	{
		// We have lost more than one frame
		send_feedback(current.feedback);
		send_feedback(next.feedback);

		current.reset(shard.frame_idx);
		next.reset(shard.frame_idx + 1);

		push_shard(std::move(shard));
	}
}

void shard_accumulator::try_submit_frame(std::optional<uint16_t> shard_idx)
{
	if (shard_idx)
		try_submit_frame(*shard_idx);
}

void shard_accumulator::try_submit_frame(uint16_t shard_idx)
{
	auto & data_shards = current.data;
	// Do not submit if the frame is not complete
	for (const auto & shard: data_shards)
	{
		if (not shard)
			return;
	}
	if (not(data_shards.back()->flags & video_stream_data_shard::end_of_frame))
		return;

	uint64_t frame_index = data_shards[0]->frame_idx;
	std::vector<std::span<const uint8_t>> payload;
	payload.reserve(data_shards.size());

	data_shard::timing_info_t timing_info{};
	for (const auto & shard: data_shards)
	{
		if (shard->timing_info)
			timing_info = *shard->timing_info;

		payload.emplace_back(shard->payload);
	}
	decoder->push_data(payload, frame_index, false);

	auto feedback = current.feedback;
	if (not data_shards.front()->view_info)
	{
		spdlog::warn("first shard has no view_info");
		return;
	}

	// Try to extract a frame
	decoder->frame_completed(feedback, timing_info, *data_shards.front()->view_info);

	advance();
}

void shard_accumulator::send_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback)
{
	auto scene = weak_scene.lock();
	if (scene)
		scene->send_feedback(feedback);
}
