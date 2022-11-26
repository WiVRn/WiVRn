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

#include "shard_accumulator.h"
#include "application.h"
#include "rs.h"
#include "scenes/stream.h"
#include "spdlog/spdlog.h"
#include "utils/typename.h"
#include "wivrn_serialization.h"

using namespace xrt::drivers::wivrn::to_headset;
using shard_set = shard_accumulator::shard_set;
using data_shard = shard_accumulator::data_shard;
using parity_shard = shard_accumulator::parity_shard;

shard_set::shard_set(uint8_t stream_index)
{
	feedback.stream_index = stream_index;
}

void shard_set::reset(uint64_t frame_index)
{
	num_shards = 0;
	min_for_reconstruction = -1;
	data.clear();
	parity.clear();

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
	if (auto i = try_reconstruct())
		return *i;
	return idx;
}

std::optional<uint16_t> shard_set::insert(parity_shard && shard)
{
	XrTime now = application::now();
	if (empty())
		feedback.received_first_packet = now;
	feedback.received_last_packet = now;

	auto idx = shard.parity_element;
	min_for_reconstruction = shard.data_shard_count;
	if (idx >= parity.size())
		parity.resize(idx + 1);
	if (not parity[idx])
		++num_shards;
	parity[idx] = std::move(shard);

	return try_reconstruct();
}

parity_shard & shard_set::get_parity()
{
	for (auto & shard: parity)
	{
		if (shard)
			return *shard;
	}
	// this function shall be called only when at least one parity shard exists
	assert(false);

	__builtin_unreachable();
}

std::optional<uint16_t> shard_set::try_reconstruct()
{
	if (parity.empty())
		return {};
	if (num_shards < min_for_reconstruction)
		return {};

	static std::once_flag once;
	std::call_once(once, reed_solomon_init);
	auto & p = get_parity();
	std::unique_ptr<reed_solomon, void (*)(reed_solomon *)> rs(
	        reed_solomon_new(p.data_shard_count, p.num_parity_elements), reed_solomon_release);

	size_t shard_size = p.payload.size();

	std::vector<uint8_t *> rs_shards;
	std::vector<uint8_t> rs_marks;

	std::vector<std::vector<uint8_t>> data_raw;
	data.resize(p.data_shard_count);
	data_raw.reserve(p.data_shard_count);
	for (auto & shard: data)
	{
		if (shard)
		{
			xrt::drivers::wivrn::serialization_packet s;
			s.serialize(*shard);
			data_raw.emplace_back(std::move(s)).resize(shard_size);
			rs_marks.push_back(0);
		}
		else
		{
			data_raw.emplace_back(shard_size, 0);
			rs_marks.push_back(1);
		}
		rs_shards.push_back(data_raw.back().data());
	}
	// WARNING: do not use p after this line, the array it points to has been modified
	parity.resize(p.num_parity_elements);
	std::vector<std::vector<uint8_t>> dummy;
	dummy.reserve(p.num_parity_elements);
	for (auto & shard: parity)
	{
		if (shard)
		{
			rs_shards.push_back(shard->payload.data());
			rs_marks.push_back(0);
		}
		else
		{
			auto d = dummy.emplace_back(shard_size).data();
			rs_shards.push_back(d);
			rs_marks.push_back(1);
		}
	}

	int err = reed_solomon_reconstruct(rs.get(), rs_shards.data(), rs_marks.data(), data.size() + parity.size(), shard_size);
	if (err)
	{
		spdlog::info("reed_solomon_reconstruct failed");
		return {};
	}
	std::optional<uint16_t> first_reconstructed;
	for (size_t i = 0; i < data.size(); ++i)
	{
		if (not data[i])
		{
			xrt::drivers::wivrn::deserialization_packet d(data_raw[i]);
			data[i] = d.deserialize<data_shard>();
			if (not first_reconstructed)
				first_reconstructed = i;
		}
	}
	assert(first_reconstructed);
	spdlog::info("reed_solomon_reconstruct success frame {}, reconstructed {} (out of {} shards)",
	             data[0]->frame_idx,
	             *first_reconstructed,
	             data.size());

	feedback.reconstructed = application::now();
	return first_reconstructed;
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
	int expected_data = -1;
	int expected_parity = -1;
	size_t data = 0;
	size_t parity = 0;
	for (const auto & shard: frame)
	{
		if (shard)
		{
			frame_idx = shard->frame_idx;
			++data;
		}
	}
	for (const auto & shard: shards.parity)
	{
		if (shard)
		{
			frame_idx = shard->frame_idx;
			expected_data = shard->data_shard_count;
			expected_parity = shard->num_parity_elements;
			++parity;
		}
	}

	spdlog::info("frame {} was not sent with {}/{} data, {}/{} parity shards)", frame_idx, data, expected_data, parity, expected_parity);
}

void shard_accumulator::advance()
{
	std::swap(current, next);
	next.reset(current.frame_index() + 1);
}

template <typename Shard>
void shard_accumulator::push_shard(Shard && shard)
{
	assert(current.frame_index() + 1 == next.frame_index());

	uint8_t frame_diff = shard.frame_idx - current.frame_index();
	if (shard.frame_idx < current.frame_index())
	{
		// frame is in the past, drop it
	}
	else if (frame_diff == 0)
	{
		// Due to error correction, inserting a shard might create a valid one before
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

template void shard_accumulator::push_shard<parity_shard>(parity_shard && shard);
template void shard_accumulator::push_shard<video_stream_data_shard>(video_stream_data_shard && shard);

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
	for (const auto & shard: data_shards)
	{
		payload.emplace_back(shard->payload);
	}
	decoder->push_data(payload, frame_index, false);

	auto feedback = current.feedback;
	assert(data_shards.back()->view_info);
	feedback.received_pose = data_shards.back()->view_info->pose;

	// Try to extract a frame
	decoder->frame_completed(feedback, *data_shards.back()->view_info);

	advance();
}

void shard_accumulator::send_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback)
{
	auto scene = weak_scene.lock();
	if (scene)
		scene->send_feedback(feedback);
}
