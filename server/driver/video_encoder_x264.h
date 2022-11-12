// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "video_encoder.h"
#include "x264.h"
#include "yuv_converter.h"

#include <mutex>
#include <list>

namespace xrt::drivers::wivrn {

class VideoEncoderX264 : public VideoEncoder
{
	x264_param_t param = {};
	x264_t *enc;

	vk_bundle *vk;
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
	VideoEncoderX264(vk_bundle *vk, encoder_settings &settings, int input_width, int input_height, float fps);

	void
	SetImages(int width,
	          int height,
	          VkFormat format,
	          int num_images,
	          VkImage *images,
	          VkImageView *views,
	          VkDeviceMemory *memory) override;

	void
	PresentImage(int index, VkCommandBuffer *out_buffer) override;

	void
	Encode(int index, bool idr, std::chrono::steady_clock::time_point pts) override;

	~VideoEncoderX264();

private:
	static void
	ProcessCb(x264_t *h, x264_nal_t *nal, void *opaque);

	void
	ProcessNal(pending_nal &&nal);

	void
	InsertInPendingNal(pending_nal &&nal);
};


} // namespace xrt::drivers::wivrn
