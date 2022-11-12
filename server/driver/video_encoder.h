// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "vk/vk_helpers.h"
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.h>

#include "encoder_settings.h"
#include "wivrn_packets.h"
#include "wivrn_session.h"

namespace xrt::drivers::wivrn {

inline const char *encoder_nvenc = "nvenc";
inline const char *encoder_vaapi = "vaapi";
inline const char *encoder_x264 = "x264";

class VideoEncoder
{
	std::mutex mutex;
	uint8_t stream_idx;
	uint64_t frame_idx;

	// temporary data
	wivrn_session *cnx;

	std::vector<to_headset::video_stream_data_shard> shards;

public:
	static std::unique_ptr<VideoEncoder>
	Create(vk_bundle *vk,
	       encoder_settings &settings,
	       uint8_t stream_idx,
	       int input_width,
	       int input_height,
	       float fps);

	virtual ~VideoEncoder() = default;

	// set input images to be encoded.
	// later referred by index only
	virtual void
	SetImages(int width,
	          int height,
	          VkFormat format,
	          int num_images,
	          VkImage *images,
	          VkImageView *views,
	          VkDeviceMemory *memory) = 0;

	// optional entrypoint, called on present to submit command buffers for the image.
	virtual void
	PresentImage(int index, VkCommandBuffer *out_buffer)
	{}

	void
	Encode(wivrn_session &cnx,
	       const to_headset::video_stream_data_shard::view_info_t &view_info,
	       uint64_t frame_index,
	       int index,
	       bool idr);


protected:
	// encode the image at provided index.
	virtual void
	Encode(int index, bool idr, std::chrono::steady_clock::time_point target_timestamp) = 0;

	void
	SendData(std::vector<uint8_t> &&data);

private:
	void
	PushShard(std::vector<uint8_t> &&payload, uint8_t flags);
};

} // namespace xrt::drivers::wivrn
