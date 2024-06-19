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

#include "encoder/yuv_converter.h"
#include "utils/wivrn_vk_bundle.h"
#include "vk/allocation.h"

#include "driver/wivrn_pacer.h"
#include "encoder/encoder_settings.h"
#include <list>
#include <memory>
#include <optional>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace xrt::drivers::wivrn
{

class VideoEncoder;

struct pseudo_swapchain
{
#ifdef __cpp_lib_atomic_lock_free_type_aliases
	using status_type = std::atomic_unsigned_lock_free;
#else
	using status_type = std::atomic_uint64_t;
#endif
	struct item
	{
		image_allocation image;
		vk::raii::ImageView image_view = nullptr;
		vk::raii::Fence fence = nullptr;
		vk::raii::CommandBuffer command_buffer = nullptr;
		yuv_converter yuv;
		status_type status; // bitmask of consumer status, index 0 for acquired, the rest for each encoder
		int64_t frame_index;
		to_headset::video_stream_data_shard::view_info_t view_info{};
	};
	std::unique_ptr<item[]> images;
	// Incremented and notified when a new image is available for encoders
	status_type present_gen;
};

struct wivrn_comp_target : public comp_target
{
	wivrn_pacer pacer;

	std::optional<wivrn_vk_bundle> wivrn_bundle;
	vk::raii::CommandPool command_pool = nullptr;

	float fps;

	int64_t current_frame_id = 0;

	pseudo_swapchain psc;

	VkColorSpaceKHR color_space;

	static std::vector<const char *> wanted_instance_extensions;
	static std::vector<const char *> wanted_device_extensions;

	struct encoder_thread
	{
		int index;
		os_thread_helper thread;
		pseudo_swapchain::status_type & gen;

		encoder_thread(pseudo_swapchain::status_type & gen) :
		        gen(gen)
		{
			os_thread_helper_init(&thread);
		}
		~encoder_thread()
		{
			os_thread_helper_signal_stop(&thread);
			gen += 1;
			gen.notify_all();
			os_thread_helper_wait_locked(&thread);
			os_thread_helper_destroy(&thread);
		}
	};
	std::vector<encoder_settings> settings;
	to_headset::video_stream_description desc{};
	std::list<encoder_thread> encoder_threads;
	std::vector<std::shared_ptr<VideoEncoder>> encoders;

	std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx;

	wivrn_comp_target(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx, struct comp_compositor * c, float fps);
	~wivrn_comp_target();

	void on_feedback(const from_headset::feedback &, const clock_offset &);
	void reset_encoders();
};

} // namespace xrt::drivers::wivrn
