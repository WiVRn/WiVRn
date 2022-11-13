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

#include "VideoEncoderFFMPEG.h"
#include <algorithm>
#include <array>
#include <stdexcept>

extern "C"
{
#include <libavcodec/avcodec.h>
}

namespace
{

bool should_keep_nal_h264(const uint8_t * header_start)
{
	uint8_t nal_type = (header_start[2] == 0 ? header_start[4] : header_start[3]) & 0x1F;
	switch (nal_type)
	{
		case 6: // supplemental enhancement information
		case 9: // access unit delimiter
			return false;
		default:
			return true;
	}
}

bool should_keep_nal_h265(const uint8_t * header_start)
{
	uint8_t nal_type = ((header_start[2] == 0 ? header_start[4] : header_start[3]) >> 1) & 0x3F;
	switch (nal_type)
	{
		case 35: // access unit delimiter
		case 39: // supplemental enhancement information
			return false;
		default:
			return true;
	}
}

void filter_NAL(const uint8_t * input, size_t input_size, std::vector<uint8_t> & out, VideoEncoderFFMPEG::Codec codec)
{
	if (input_size < 4)
		return;
	std::array<uint8_t, 3> header = {{0, 0, 1}};
	auto end = input + input_size;
	auto header_start = input;
	while (header_start != end)
	{
		auto next_header = std::search(header_start + 3, end, header.begin(), header.end());
		if (next_header != end and next_header[-1] == 0)
		{
			next_header--;
		}
		if (codec == VideoEncoderFFMPEG::Codec::h264 and should_keep_nal_h264(header_start))
			out.insert(out.end(), header_start, next_header);
		if (codec == VideoEncoderFFMPEG::Codec::h265 and should_keep_nal_h265(header_start))
			out.insert(out.end(), header_start, next_header);
		header_start = next_header;
	}
}

} // namespace

void VideoEncoderFFMPEG::Encode(int index, bool idr, std::chrono::steady_clock::time_point target_timestamp)
{
	PushFrame(index, idr, target_timestamp);
	AVPacket * enc_pkt = av_packet_alloc();
	int err = avcodec_receive_packet(encoder_ctx.get(), enc_pkt);
	if (err == 0)
	{
		SendData({enc_pkt->data, enc_pkt->data + enc_pkt->size});
		av_packet_free(&enc_pkt);
	}
	if (err == AVERROR(EAGAIN))
	{
		return;
	}
	if (err)
	{
		throw std::runtime_error("frame encoding failed, code " + std::to_string(err));
	}
}
