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

#include "video_encoder.h"
#include "vk/allocation.h"
#include "x264.h"

#include <list>
#include <mutex>
#include <vulkan/vulkan_raii.hpp>

namespace xrt::drivers::wivrn
{

class VideoEncoderX264 : public VideoEncoder
{
	x264_param_t param = {};
	x264_t * enc;

	x264_picture_t pic_in;
	x264_picture_t pic_out = {};

	buffer_allocation luma;
	buffer_allocation chroma;
	uint32_t chroma_width;

	vk::Rect2D rect;

	struct pending_nal
	{
		int first_mb;
		int last_mb;
		std::vector<uint8_t> data;
	};

	std::mutex mutex;
	int next_mb;
	int num_mb; // Number of macroblocks in a frame
	std::list<pending_nal> pending_nals;

public:
	VideoEncoderX264(wivrn_vk_bundle& vk, encoder_settings & settings, float fps);

	void PresentImage(yuv_converter & src_yuv, vk::raii::CommandBuffer & cmd_buf) override;

	void Encode(bool idr, std::chrono::steady_clock::time_point pts) override;

	~VideoEncoderX264();

private:
	static void ProcessCb(x264_t * h, x264_nal_t * nal, void * opaque);

	void ProcessNal(pending_nal && nal);

	void InsertInPendingNal(pending_nal && nal);
};

} // namespace xrt::drivers::wivrn
