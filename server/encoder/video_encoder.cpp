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

// Include first because of incompatibility between Eigen and X includes
#include "driver/wivrn_session.h"

#include "os/os_time.h"
#include "video_encoder.h"

#include <string>

#include "wivrn_config.h"

#ifdef WIVRN_USE_NVENC
#include "video_encoder_nvenc.h"
#endif
#ifdef WIVRN_USE_VAAPI
#include "ffmpeg/video_encoder_va.h"
#endif
#ifdef WIVRN_USE_X264
#include "video_encoder_x264.h"
#endif

namespace xrt::drivers::wivrn
{

std::unique_ptr<VideoEncoder> VideoEncoder::Create(
        wivrn_vk_bundle & wivrn_vk,
        encoder_settings & settings,
        uint8_t stream_idx,
        int input_width,
        int input_height,
        float fps)
{
	using namespace std::string_literals;
	std::unique_ptr<VideoEncoder> res;
#ifdef WIVRN_USE_X264
	if (settings.encoder_name == encoder_x264)
	{
		res = std::make_unique<VideoEncoderX264>(wivrn_vk, settings, fps);
	}
#endif
#ifdef WIVRN_USE_NVENC
	if (settings.encoder_name == encoder_nvenc)
	{
		res = std::make_unique<VideoEncoderNvenc>(wivrn_vk, settings, fps);
	}
#endif
#ifdef WIVRN_USE_VAAPI
	if (settings.encoder_name == encoder_vaapi)
	{
		res = std::make_unique<video_encoder_va>(wivrn_vk, settings, fps);
	}
#endif
	if (not res)
		throw std::runtime_error("Failed to create encoder " + settings.encoder_name);
	res->stream_idx = stream_idx;
	return res;
}

void VideoEncoder::SyncNeeded()
{
	sync_needed = true;
}

void VideoEncoder::Encode(wivrn_session & cnx,
                          const to_headset::video_stream_data_shard::view_info_t & view_info,
                          uint64_t frame_index)
{
	this->cnx = &cnx;
	auto target_timestamp = std::chrono::steady_clock::time_point(std::chrono::nanoseconds(view_info.display_time));
	bool idr = sync_needed.exchange(false);
	const char * extra = idr ? ",idr" : ",p";
	cnx.dump_time("encode_begin", frame_index, os_monotonic_get_ns(), stream_idx, extra);

	// Prepare the video shard template
	shard.stream_item_idx = stream_idx;
	shard.frame_idx = frame_index;
	shard.shard_idx = 0;
	shard.view_info = view_info;

	Encode(idr, target_timestamp);
	cnx.dump_time("encode_end", frame_index, os_monotonic_get_ns(), stream_idx, extra);
}

void VideoEncoder::SendData(std::span<uint8_t> data, bool end_of_frame)
{
	std::lock_guard lock(mutex);
#if 0
	std::ofstream debug("/tmp/video_dump-" + std::to_string(stream_idx), std::ios::app);
	debug.write((char*)data.data(), data.size());
#endif
	if (shard.shard_idx == 0)
		cnx->dump_time("send_begin", shard.frame_idx, os_monotonic_get_ns(), stream_idx);

	shard.flags = to_headset::video_stream_data_shard::start_of_slice;
	auto begin = data.begin();
	auto end = data.end();
	while (begin != end)
	{
		const size_t view_info_size = sizeof(to_headset::video_stream_data_shard::view_info_t);
		const size_t max_payload_size = to_headset::video_stream_data_shard::max_payload_size - (shard.view_info ? view_info_size : 0);
		auto next = std::min(end, begin + max_payload_size);
		if (next == end)
		{
			shard.flags |= to_headset::video_stream_data_shard::end_of_slice;
			if (end_of_frame)
				shard.flags |= to_headset::video_stream_data_shard::end_of_frame;
		}
		shard.payload = {begin, next};
		cnx->send_stream(shard);
		++shard.shard_idx;
		shard.flags = 0;
		shard.view_info.reset();
		begin = next;
	}
	if (end_of_frame)
		cnx->dump_time("send_end", shard.frame_idx, os_monotonic_get_ns(), stream_idx);
}

} // namespace xrt::drivers::wivrn
