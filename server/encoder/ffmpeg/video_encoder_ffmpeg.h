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

namespace wivrn
{

class video_encoder_ffmpeg : public wivrn::video_encoder
{
public:
	std::optional<data> encode(uint8_t slot, uint64_t frame_index) override;

	struct mute_logs
	{
		mute_logs();
		~mute_logs();
	};

protected:
	video_encoder_ffmpeg(uint8_t stream_idx, to_headset::video_stream_description::channels_t channels, double bitrate_multiplier) :
	        wivrn::video_encoder(stream_idx, channels, std::make_unique<default_idr_handler>(), bitrate_multiplier, true) {}

	virtual void push_frame(bool idr, uint8_t slot) = 0;

	av_codec_context_ptr encoder_ctx;

private:
	static bool once;
};
} // namespace wivrn
