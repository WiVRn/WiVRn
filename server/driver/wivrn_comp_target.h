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

#include "wivrn_packets.h"
#include "wivrn_session.h"

#include "main/comp_target.h"
#include "vk/vk_cmd_pool.h"

#include <condition_variable>
#include <list>
#include <memory>
#include <vector>
#include "driver/wivrn_pacer.h"

namespace xrt::drivers::wivrn
{

class VideoEncoder;

struct pseudo_swapchain
{
	struct pseudo_swapchain_memory
	{
		VkFence fence;
		VkDeviceMemory memory;
		VkCommandBuffer present_cmd;
		uint8_t status; // bitmask of consumer status, index 0 for acquired, the rest for each encoder
		uint64_t frame_index;
		to_headset::video_stream_data_shard::view_info_t view_info{};
	} * images{};
	std::mutex mutex;
	std::condition_variable cv;
};

struct wivrn_comp_target : public comp_target
{
	wivrn_pacer pacer;

	vk_cmd_pool pool;

	float fps;

	int64_t current_frame_id;

	// Monotonic counter, for video stream
	uint64_t frame_index = 0;

	pseudo_swapchain psc;

	VkColorSpaceKHR color_space;

	struct encoder_thread
	{
		int index;
		os_thread_helper thread;

		encoder_thread()
		{
			os_thread_helper_init(&thread);
		}
		~encoder_thread()
		{
			os_thread_helper_stop_and_wait(&thread);
			os_thread_helper_destroy(&thread);
		}
	};
	std::list<encoder_thread> encoder_threads;
	std::vector<std::shared_ptr<VideoEncoder>> encoders;

	std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx;

	wivrn_comp_target(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx, struct comp_compositor * c, float fps);

	void on_feedback(const from_headset::feedback &, const clock_offset &);
};

} // namespace xrt::drivers::wivrn
