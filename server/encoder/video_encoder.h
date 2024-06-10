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

#include "driver/clock_offset.h"
#include "wivrn_config.h"
#include "wivrn_packets.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{

struct encoder_settings;
struct wivrn_vk_bundle;
class wivrn_session;

inline const char * encoder_nvenc = "nvenc";
inline const char * encoder_vaapi = "vaapi";
inline const char * encoder_x264 = "x264";
inline const char * encoder_vulkan = "vulkan";

class video_encoder
{
protected:
	struct data
	{
		video_encoder * encoder;
		std::span<uint8_t> span;
		std::shared_ptr<void> mem;
	};

private:
	class sender
	{
		std::mutex mutex;
		std::condition_variable cv;
		std::deque<data> pending;
		std::jthread thread;
		void run();
		sender();

	public:
		void push(data &&);
		static std::shared_ptr<sender> get();
		void wait_idle(video_encoder *);
	};

public:
	const uint8_t stream_idx;
	const to_headset::video_stream_description::channels_t channels;
	static const uint8_t num_slots = 2;

private:
	std::mutex mutex;
	std::array<std::atomic<bool>, num_slots> busy = {false, false};
	uint8_t next_present = 0;
	uint8_t next_encode = 0;

	// temporary data
	wivrn_session * cnx;

	// shard to send
	to_headset::video_stream_data_shard shard;

	to_headset::video_stream_data_shard::timing_info_t timing_info;
	clock_offset clock;

	std::atomic_bool sync_needed = true;
	uint64_t last_idr_frame;

	std::ofstream video_dump;

	std::shared_ptr<sender> shared_sender;

public:
	static std::unique_ptr<video_encoder> create(
	        wivrn_vk_bundle &,
	        encoder_settings & settings,
	        uint8_t stream_idx,
	        int input_width,
	        int input_height,
	        float fps);

#if WIVRN_USE_VULKAN_ENCODE
	static std::pair<std::vector<vk::VideoProfileInfoKHR>, vk::ImageUsageFlags> get_create_image_info(const std::vector<encoder_settings> &);
#endif

	video_encoder(uint8_t stream_idx, to_headset::video_stream_description::channels_t channels, bool async_send);
	virtual ~video_encoder();

	void present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf);
	// for vulkan video (command buffer is on a video queue)
	void present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, vk::Fence fence, uint64_t frame_index);

	virtual void on_feedback(const from_headset::feedback &);
	virtual void reset();

	void Encode(wivrn_session & cnx,
	            const to_headset::video_stream_data_shard::view_info_t & view_info,
	            uint64_t frame_index);

protected:
	// called on present to submit command buffers for the image.
	virtual void present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot) {};
	// for vulkan video (command buffer is on a video queue)
	virtual void present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, vk::Fence, uint8_t slot, uint64_t frame_index) {};
	// called when command buffer finished executing
	virtual std::optional<data> encode(bool idr, std::chrono::steady_clock::time_point target_timestamp, uint8_t slot) = 0;

	void SendData(std::span<uint8_t> data, bool end_of_frame);
};

} // namespace wivrn
