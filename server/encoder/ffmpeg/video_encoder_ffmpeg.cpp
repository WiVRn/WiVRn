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

#include "video_encoder_ffmpeg.h"
#include "util/u_logging.h"
#include <stdexcept>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/log.h>
}

namespace
{

bool set_log_level()
{
	const char * level = getenv("FFMPEG_LOG_LEVEL");
	if (level)
	{
		if (strcasecmp(level, "TRACE") == 0)
			av_log_set_level(AV_LOG_TRACE);
		else if (strcasecmp(level, "DEBUG") == 0)
			av_log_set_level(AV_LOG_DEBUG);
		else if (strcasecmp(level, "VERBOSE") == 0)
			av_log_set_level(AV_LOG_VERBOSE);
		else if (strcasecmp(level, "INFO") == 0)
			av_log_set_level(AV_LOG_INFO);
		else if (strcasecmp(level, "WARNING") == 0)
			av_log_set_level(AV_LOG_WARNING);
		else if (strcasecmp(level, "ERROR") == 0)
			av_log_set_level(AV_LOG_ERROR);
		else if (strcasecmp(level, "FATAL") == 0)
			av_log_set_level(AV_LOG_FATAL);
		else if (strcasecmp(level, "PANIC") == 0)
			av_log_set_level(AV_LOG_PANIC);
		else if (strcasecmp(level, "QUIET") == 0)
			av_log_set_level(AV_LOG_QUIET);
		else
			U_LOG_W("log level %s not recognized for FFMPEG_LOG_LEVEL", level);
	}
	return true;
}

} // namespace

namespace wivrn
{

bool video_encoder_ffmpeg::once = set_log_level();

std::optional<wivrn::video_encoder::data> video_encoder_ffmpeg::encode(bool idr, std::chrono::steady_clock::time_point target_timestamp, uint8_t slot)
{
	if (auto bitrate = pending_bitrate.exchange(BITRATE_UNCHANGED))
	{
		idr = true;
		encoder_ctx->bit_rate = bitrate;
		encoder_ctx->rc_max_rate = bitrate;
	}

	push_frame(idr, target_timestamp, slot);
	std::shared_ptr<AVPacket> enc_pkt(av_packet_alloc(), [](AVPacket * d) { av_packet_free(&d); });
	int err = avcodec_receive_packet(encoder_ctx.get(), enc_pkt.get());
	if (err == 0)
	{
		return data{
		        .encoder = this,
		        .span = std::span(enc_pkt->data, enc_pkt->size),
		        .mem = std::move(enc_pkt), // elements are evaluated in order
		};
	}
	if (err == AVERROR(EAGAIN))
	{
		U_LOG_W("EAGAIN in encoder %d", stream_idx);
		return {};
	}
	throw std::runtime_error("frame encoding failed, code " + std::to_string(err));
}

video_encoder_ffmpeg::mute_logs::mute_logs()
{
	if (not getenv("FFMPEG_LOG_LEVEL"))
		av_log_set_level(AV_LOG_QUIET);
}

video_encoder_ffmpeg::mute_logs::~mute_logs()
{
	set_log_level();
}
} // namespace wivrn
