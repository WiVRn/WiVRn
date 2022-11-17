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

#include "nvEncodeAPI.h"
#include "video_encoder.h"
#include <cuda.h>

namespace xrt::drivers::wivrn
{

class VideoEncoderNvenc : public VideoEncoder
{
	vk_bundle * vk;
	// relevant part of the input image to encode
	int offset_x;
	int offset_y;
	int width;
	int height;

	NV_ENCODE_API_FUNCTION_LIST fn;
	CUcontext cuda;
	void * session_handle;
	NV_ENC_OUTPUT_PTR bitstreamBuffer;

	struct image_data
	{
		CUmipmappedArray cuda_image;
		CUarray cuda_array;
	};
	std::vector<image_data> images;
	CUdeviceptr frame;
	size_t pitch;
	NV_ENC_REGISTERED_PTR nvenc_resource;
	video_codec codec;
	float fps;
	int bitrate;

	bool supports_frame_invalidation;

public:
	VideoEncoderNvenc(vk_bundle * vk, const encoder_settings & settings, float fps);

	void SetImages(int width,
	               int height,
	               VkFormat format,
	               int num_images,
	               VkImage * images,
	               VkImageView * views,
	               VkDeviceMemory * memory) override;

	void Encode(int index, bool idr, std::chrono::steady_clock::time_point pts) override;
};

} // namespace xrt::drivers::wivrn
