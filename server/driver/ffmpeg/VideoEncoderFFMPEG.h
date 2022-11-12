// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <cstdint>
#include <chrono>
#include <vector>
#include "ffmpeg_helper.h"
#include "../video_encoder.h"

class VideoEncoderFFMPEG : public VideoEncoder
{
public:
	using Codec = xrt::drivers::wivrn::video_codec;

	void
	Encode(int index, bool idr, std::chrono::steady_clock::time_point target_timestamp) override;

protected:
	virtual void
	PushFrame(uint32_t frame_index, bool idr, std::chrono::steady_clock::time_point pts) = 0;

	av_codec_context_ptr encoder_ctx;
	Codec codec;
};
