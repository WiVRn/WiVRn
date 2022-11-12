// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "video_encoder.h"
#include "external/nvEncodeAPI.h"
#include <cuda.h>

namespace xrt::drivers::wivrn {

class VideoEncoderNvenc : public VideoEncoder
{
	vk_bundle *vk;
	// relevant part of the input image to encode
	int offset_x;
	int offset_y;
	int width;
	int height;

	NV_ENCODE_API_FUNCTION_LIST fn;
	CUcontext cuda;
	void *session_handle;
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
	VideoEncoderNvenc(vk_bundle *vk, const encoder_settings &settings, float fps);

	void
	SetImages(int width,
	          int height,
	          VkFormat format,
	          int num_images,
	          VkImage *images,
	          VkImageView *views,
	          VkDeviceMemory *memory) override;

	void
	Encode(int index, bool idr, std::chrono::steady_clock::time_point pts) override;
};


} // namespace xrt::drivers::wivrn
