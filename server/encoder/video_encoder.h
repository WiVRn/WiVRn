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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include <vulkan/vulkan_raii.hpp>

#include "encoder_settings.h"
#include "wivrn_packets.h"

class yuv_converter;
struct wivrn_vk_bundle;

namespace xrt::drivers::wivrn
{

class wivrn_session;

inline const char * encoder_nvenc = "nvenc";
inline const char * encoder_vaapi = "vaapi";
inline const char * encoder_x264 = "x264";

class VideoEncoder
{
protected:
	struct data
	{
		VideoEncoder * encoder;
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
		void wait_idle(VideoEncoder *);
	};

protected:
	uint8_t stream_idx;

private:
	std::mutex mutex;

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
	static std::unique_ptr<VideoEncoder> Create(
	        wivrn_vk_bundle &,
	        encoder_settings & settings,
	        uint8_t stream_idx,
	        int input_width,
	        int input_height,
	        float fps);

	VideoEncoder(bool async_send = false);
	virtual ~VideoEncoder();

	// called on present to submit command buffers for the image.
	virtual void PresentImage(yuv_converter & src_yuv, vk::raii::CommandBuffer & cmd_buf) = 0;

	// The other end lost a frame and needs to resynchronize
	void SyncNeeded();

	void Encode(wivrn_session & cnx,
	            const to_headset::video_stream_data_shard::view_info_t & view_info,
	            uint64_t frame_index);

protected:
	// called when command buffer finished executing
	virtual std::optional<data> encode(bool idr, std::chrono::steady_clock::time_point target_timestamp) = 0;

	void SendData(std::span<uint8_t> data, bool end_of_frame);
};

} // namespace xrt::drivers::wivrn
