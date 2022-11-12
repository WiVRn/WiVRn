// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "VideoEncoderFFMPEG.h"
#include "vk/vk_helpers.h"
#include "ffmpeg_helper.h"

extern "C" {
#include <libavutil/version.h>
}

// DRM format modifiers are not implemented before ffmpeg 5
inline const bool use_drm_format_modifiers = LIBAVUTIL_VERSION_MAJOR >= 57;

struct AVFilterContext;

class VideoEncoderVA : public VideoEncoderFFMPEG
{
public:
	VideoEncoderVA(vk_bundle *vk, const encoder_settings &settings, float fps);

	void
	SetImages(int width,
	          int height,
	          VkFormat format,
	          int num_images,
	          VkImage *images,
	          VkImageView *views,
	          VkDeviceMemory *memory) override;

	void
	PushFrame(uint32_t frame_index, bool idr, std::chrono::steady_clock::time_point pts) override;

private:
	void
	InitFilterGraph();

	vk_bundle *vk;
	int width;
	int height;
	// relevant part of the input image to encode
	int offset_x;
	int offset_y;
	av_buffer_ptr hw_ctx_vaapi;
	std::vector<av_frame_ptr> mapped_frames;
	av_filter_graph_ptr filter_graph;
	AVFilterContext *filter_in = nullptr;
	AVFilterContext *filter_out = nullptr;
};
