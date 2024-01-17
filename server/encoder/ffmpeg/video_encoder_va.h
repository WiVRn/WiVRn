/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "video_encoder_ffmpeg.h"
#include "ffmpeg_helper.h"
#include "utils/wivrn_vk_bundle.h"
#include <vulkan/vulkan_raii.hpp>

namespace xrt::drivers::wivrn
{
struct encoder_settings;
}

class video_encoder_va : public VideoEncoderFFMPEG
{
	av_buffer_ptr drm_frame_ctx;
	av_frame_ptr va_frame;
	av_frame_ptr drm_frame;
	vk::Rect2D rect;
	vk::raii::Image luma;
	vk::raii::Image chroma;
	std::vector<vk::raii::DeviceMemory> mem;

public:
	video_encoder_va(wivrn_vk_bundle&, xrt::drivers::wivrn::encoder_settings & settings, float fps);

	void PresentImage(yuv_converter & src_yuv, vk::raii::CommandBuffer & cmd_buf) override;

protected:
	void PushFrame(bool idr, std::chrono::steady_clock::time_point pts) override;
};
