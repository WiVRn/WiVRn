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

#include "encoder/video_encoder.h"
#include "ffmpeg_helper.h"
#include <chrono>

class VideoEncoderFFMPEG : public xrt::drivers::wivrn::VideoEncoder
{
public:
	using Codec = xrt::drivers::wivrn::video_codec;

	std::optional<data> encode(bool idr, std::chrono::steady_clock::time_point target_timestamp, uint8_t slot) override;

	struct mute_logs
	{
		mute_logs();
		~mute_logs();
	};

protected:
	VideoEncoderFFMPEG() :
	        xrt::drivers::wivrn::VideoEncoder(true) {}

	virtual void push_frame(bool idr, std::chrono::steady_clock::time_point pts, uint8_t slot) = 0;

	av_codec_context_ptr encoder_ctx;

private:
	static bool once;
};
