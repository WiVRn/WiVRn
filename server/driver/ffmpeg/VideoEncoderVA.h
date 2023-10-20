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

#include "VideoEncoderFFMPEG.h"
#include "ffmpeg_helper.h"
#include "vk/vk_helpers.h"

extern "C"
{
#include <libavutil/version.h>
}

// DRM format modifiers are not implemented before ffmpeg 5
inline const bool use_drm_format_modifiers = LIBAVUTIL_VERSION_MAJOR >= 57;

struct AVFilterContext;

class VideoEncoderVA : public VideoEncoderFFMPEG
{
public:
	VideoEncoderVA(vk_bundle * vk, const xrt::drivers::wivrn::encoder_settings & settings, float fps);

	void
	SetImages(int width,
	          int height,
	          VkFormat format,
	          int num_images,
	          VkImage * images,
	          VkImageView * views,
	          VkDeviceMemory * memory) override;

	void
	PushFrame(uint32_t frame_index, bool idr, std::chrono::steady_clock::time_point pts) override;

private:
	void
	InitFilterGraph();

	vk_bundle * vk;
	int width;
	int height;
	// relevant part of the input image to encode
	int offset_x;
	int offset_y;
	av_buffer_ptr hw_ctx_vaapi;
	std::vector<av_frame_ptr> mapped_frames;
	av_filter_graph_ptr filter_graph;
	AVFilterContext * filter_in = nullptr;
	AVFilterContext * filter_out = nullptr;
};
