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
#include "x264.h"
#include "yuv_converter.h"

#include <list>
#include <mutex>

namespace xrt::drivers::wivrn
{

class VideoEncoderX264 : public VideoEncoder
{
	x264_param_t param = {};
	x264_t * enc;

	vk_bundle * vk;
	x264_picture_t pic_in;
	x264_picture_t pic_out = {};
	std::unique_ptr<YuvConverter> converter;

	struct pending_nal
	{
		int first_mb;
		int last_mb;
		std::vector<uint8_t> data;
	};

	std::mutex mutex;
	int next_mb;
	std::list<pending_nal> pending_nals;

public:
	VideoEncoderX264(vk_bundle * vk, encoder_settings & settings, int input_width, int input_height, float fps);

	void SetImages(int width,
	               int height,
	               VkFormat format,
	               int num_images,
	               VkImage * images,
	               VkImageView * views,
	               VkDeviceMemory * memory) override;

	void PresentImage(int index, VkCommandBuffer * out_buffer) override;

	void Encode(int index, bool idr, std::chrono::steady_clock::time_point pts) override;

	~VideoEncoderX264();

private:
	static void ProcessCb(x264_t * h, x264_nal_t * nal, void * opaque);

	void ProcessNal(pending_nal && nal);

	void InsertInPendingNal(pending_nal && nal);
};

} // namespace xrt::drivers::wivrn
