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
#include "scenes/stream.h" // IWYU pragma: keep
#include "spdlog/spdlog.h"
#include "xr/instance.h"

namespace wivrn
{

using namespace wivrn::to_headset;
using shard_set = shard_accumulator::shard_set;
using data_shard = shard_accumulator::data_shard;

shard_set::shard_set(uint8_t stream_index)
{
	feedback.stream_index = stream_index;
}

void shard_set::reset(uint64_t frame_index)
{
	min_for_reconstruction = -1;
	data.clear();

	uint8_t stream_index = feedback.stream_index;
	feedback = {};
	feedback.frame_index = frame_index;
	feedback.stream_index = stream_index;
}

bool shard_set::empty() const
{
	return data.empty();
}

static bool is_complete(const shard_set & shards)
{
	const auto & frame = shards.data;
	if (frame.empty())
		return false;
	if (not(frame.back() and frame.back()->timing_info))
		return false;
	for (const auto & shard: frame)
		if (not shard)
			return false;
	return true;
}

std::optional<uint16_t> shard_set::insert(data_shard && shard, xr::instance & instance)
{
	if (empty())
		feedback.received_first_packet = instance.now();

	auto idx = shard.shard_idx;
	if (idx >= data.size())
		data.resize(idx + 1);
	if (data[idx])
		return {};
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
	size_t missing = 0;
	for (const auto & shard: frame)
	{
		if (shard)
		{
			frame_idx = shard->frame_idx;
			++data;
		}
		else
			++missing;
	}

	bool end = frame.back() and frame.back()->timing_info;
	spdlog::info("frame {} was not sent with {} data shards, {}{} missing", frame_idx, data, end ? "" : "at least ", missing);
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
		spdlog::info("Drop shard for old frame {} (current {})", shard.frame_idx, current.frame_index());
	}
	else if (frame_diff == 0)
	{
		auto shard_idx = current.insert(std::move(shard), instance);
		try_submit_frame(shard_idx);
	}
	else if (frame_diff == 1)
	{
		next.insert(std::move(shard), instance);
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

	for (size_t idx = 0; idx < shard_idx; ++idx)
		if (not data_shards[idx])
			return;

	uint16_t last_idx = shard_idx + 1;
	for (size_t size = data_shards.size();
	     last_idx < size and data_shards[last_idx];
	     ++last_idx)
	{
	}

	std::vector<std::span<const uint8_t>> payload;
	payload.reserve(last_idx - shard_idx);
	for (size_t idx = shard_idx; idx < last_idx; ++idx)
		payload.emplace_back(data_shards[idx]->payload);

	bool frame_complete = last_idx == data_shards.size() and data_shards.back()->timing_info;
	decoder_->push_data(payload, data_shards[shard_idx]->frame_idx, not frame_complete);

	if (not frame_complete)
		return;

	current.feedback.received_last_packet = instance.now();
	current.feedback.sent_to_decoder = current.feedback.received_last_packet;
	data_shard::timing_info_t timing_info = data_shards.back()->timing_info.value_or(data_shard::timing_info_t{});
	current.feedback.encode_begin = timing_info.encode_begin;
	current.feedback.encode_end = timing_info.encode_end;
	current.feedback.send_begin = timing_info.send_begin;
	current.feedback.send_end = timing_info.send_end;

	if (not data_shards.front()->view_info)
	{
		spdlog::warn("first shard has no view_info");
		return;
	}

	// Try to extract a frame
	decoder_->frame_completed(current.feedback, *data_shards.front()->view_info);

	send_feedback(current.feedback);

	advance();
}

void shard_accumulator::send_feedback(wivrn::from_headset::feedback & feedback)
{
	if (not feedback.received_last_packet)
		feedback.received_first_packet = instance.now();
	auto scene = weak_scene.lock();
	if (scene)
		scene->send_feedback(feedback);
}
} // namespace wivrn
