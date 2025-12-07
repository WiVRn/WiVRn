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
#include "idr_handler.h"
#include "wivrn_packets.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
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
inline const char * encoder_raw = "raw";

class video_encoder
{
protected:
	struct data
	{
		video_encoder * encoder;
		std::span<uint8_t> span;
		std::shared_ptr<void> mem;
		// true if data should be sent over reliable (TCP) socket
		bool prefer_control = false;
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
	const double bitrate_multiplier;

private:
	using state_t = std::atomic_unsigned_lock_free::value_type;
	static const state_t idle = 0;
	static const state_t busy = 1;
	static const state_t skip = 2;
	std::array<std::atomic_unsigned_lock_free, num_slots> state = {idle, idle};
	uint8_t present_slot = 0;
	uint8_t encode_slot = 0;

	std::mutex mutex;

	// temporary data
	wivrn_session * cnx;

	// shard to send
	to_headset::video_stream_data_shard shard;

	to_headset::video_stream_data_shard::timing_info_t timing_info;
	clock_offset clock;

	std::ofstream video_dump;

	std::shared_ptr<sender> shared_sender;

protected:
	std::atomic_uint32_t pending_bitrate;
	std::atomic<float> pending_framerate;
	std::unique_ptr<idr_handler> idr;

public:
	static std::unique_ptr<video_encoder> create(
	        wivrn_vk_bundle &,
	        encoder_settings & settings,
	        uint8_t stream_idx,
	        int input_width,
	        int input_height,
	        float fps);

	video_encoder(uint8_t stream_idx, to_headset::video_stream_description::channels_t channels, std::unique_ptr<idr_handler>, double bitrate_multiplier, bool async_send);
	virtual ~video_encoder();

	// return value: true if image should be transitioned to queue and layout for vulkan video encode
	// semaphore to be signaled by the compositor
	std::pair<bool, vk::Semaphore> present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint64_t frame_index);
	void post_submit();

	void on_feedback(const from_headset::feedback &);
	void reset();

	void set_bitrate(uint32_t bitrate_bps);
	void set_framerate(float framerate);

	void encode(wivrn_session & cnx,
	            const to_headset::video_stream_data_shard::view_info_t & view_info,
	            uint64_t frame_index);

	// called on present to submit command buffers for the image.
	virtual std::pair<bool, vk::Semaphore> present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t frame_index) = 0;
	// called after command buffer passed in present_image was submitted
	virtual void post_submit(uint8_t slot) {}
	// called when command buffer finished executing
	virtual std::optional<data> encode(uint8_t slot, uint64_t frame_index) = 0;

	void SendData(std::span<uint8_t> data, bool end_of_frame, bool control = false);
};

} // namespace wivrn
