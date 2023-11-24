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

#include "vk/vk_cmd_pool.h"
#include "vk/vk_helpers.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vulkan/vulkan.h>

#include "encoder_settings.h"
#include "wivrn_packets.h"

namespace xrt::drivers::wivrn
{

class wivrn_session;

inline const char * encoder_nvenc = "nvenc";
inline const char * encoder_vaapi = "vaapi";
inline const char * encoder_x264 = "x264";

class VideoEncoder
{
	std::mutex mutex;

protected:
	uint8_t stream_idx;

private:
	// temporary data
	wivrn_session * cnx;

	// shard to send
	to_headset::video_stream_data_shard shard;

	std::atomic_bool sync_needed = true;

public:
	static std::unique_ptr<VideoEncoder> Create(vk_bundle * vk,
	                                            vk_cmd_pool & pool,
	                                            encoder_settings & settings,
	                                            uint8_t stream_idx,
	                                            int input_width,
	                                            int input_height,
	                                            float fps);

	virtual ~VideoEncoder() = default;

	// set input images to be encoded.
	// later referred by index only
	virtual void SetImages(int width,
	                       int height,
	                       VkFormat format,
	                       int num_images,
	                       VkImage * images,
	                       VkImageView * views,
	                       VkDeviceMemory * memory) = 0;

	// optional entrypoint, called on present to submit command buffers for the image.
	virtual void PresentImage(int index, VkCommandBuffer * out_buffer)
	{}

	// The other end lost a frame and needs to resynchronize
	void SyncNeeded();

	void Encode(wivrn_session & cnx,
	            const to_headset::video_stream_data_shard::view_info_t & view_info,
	            uint64_t frame_index,
	            int index);

protected:
	// encode the image at provided index.
	virtual void Encode(int index, bool idr, std::chrono::steady_clock::time_point target_timestamp) = 0;

	void SendData(std::span<uint8_t> data, bool end_of_frame);
};

} // namespace xrt::drivers::wivrn
