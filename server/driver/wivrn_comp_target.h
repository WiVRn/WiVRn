/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2026  Sapphire <imsapphire0@gmail.com>
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

#include "main/comp_compositor.h"
#include "main/comp_target.h"

#include "encoder/encoder_settings.h"
#include "utils/wivrn_vk_bundle.h"
#include "vk/allocation.h"
#include "wivrn_foveation.h"
#include "wivrn_pacer.h"
#include "wivrn_packets.h"

#include <array>
#include <list>
#include <memory>
#include <optional>
#include <thread>
#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{

class wivrn_session;
class video_encoder;

struct pseudo_swapchain
{
#ifdef __cpp_lib_atomic_lock_free_type_aliases
	using status_type = std::atomic_unsigned_lock_free;
#else
	using status_type = std::atomic_uint64_t;
#endif
	enum class status_t
	{
		free,
		acquired,
		encoding,
	};
	struct item
	{
		image_allocation image;
		vk::raii::ImageView image_view_y = nullptr;
		vk::raii::ImageView image_view_cbcr = nullptr;
		status_t status;
	};
	std::vector<item> images;

	// bitmask of encoder status, first bit to request exit, then one bit per thread:
	// 0 when encoder is done
	// 1 when busy/image to be encoded
	status_type status;

	// Data to be encoded
	vk::raii::Fence fence = nullptr;
	vk::raii::CommandBuffer command_buffer = nullptr;

	int64_t frame_index;
	to_headset::video_stream_data_shard::view_info_t view_info{};
};

class wivrn_comp_target : public comp_target
{
	to_headset::video_stream_description desc{};

	std::optional<wivrn_vk_bundle> wivrn_bundle;
	vk::raii::CommandPool command_pool = nullptr;

	int64_t current_frame_id = 0;

	pseudo_swapchain psc;

	VkColorSpaceKHR color_space;

	std::array<encoder_settings, 3> settings;
	std::list<std::jthread> encoder_threads;
	std::vector<std::shared_ptr<video_encoder>> encoders;

	wivrn::wivrn_session & cnx;

	std::atomic<float> requested_refresh_rate;
	std::atomic<bool> skip_encoding;

	inline vk_bundle * get_vk()
	{
		return &c->base.vk;
	}

	void init_semaphores();
	void fini_semaphores();
	VkResult create_images_impl(vk::ImageUsageFlags flags);
	void destroy_images();
	void create_encoders();

	void run_present(std::stop_token stop_token, int index, std::vector<std::shared_ptr<video_encoder>> encoders);

	bool init_pre_vulkan();
	bool init_post_vulkan(uint32_t preferred_width, uint32_t preferred_height);
	bool check_ready();
	void create_images(const struct comp_target_create_images_info * create_info, struct vk_bundle_queue * present_queue);
	bool has_images();
	VkResult acquire(uint32_t * out_index);
	VkResult present(struct vk_bundle_queue * present_queue,
	                 uint32_t index,
	                 uint64_t timeline_semaphore_value,
	                 int64_t desired_present_time_ns,
	                 int64_t present_slop_ns);
	void flush();
	void calc_frame_pacing(int64_t * out_frame_id,
	                       int64_t * out_wake_up_time_ns,
	                       int64_t * out_desired_present_time_ns,
	                       int64_t * out_present_slop_ns,
	                       int64_t * out_predicted_display_time_ns);
	void mark_timing_point(enum comp_target_timing_point point,
	                       int64_t frame_id,
	                       int64_t when_ns);
	VkResult update_timings();
	void info_gpu(int64_t frame_id, int64_t gpu_start_ns, int64_t gpu_end_ns, int64_t when_ns);
	void set_title(const char * title);
	xrt_result_t get_refresh_rates(uint32_t * count, float * refresh_rates_hz);
	xrt_result_t get_current_refresh_rate(float * refresh_rate_hz);
	xrt_result_t request_refresh_rate(float refresh_rate_hz);
	void destroy();

public:
	static std::vector<const char *> wanted_instance_extensions;
	static std::vector<const char *> wanted_device_extensions;

	wivrn_pacer pacer;
	std::optional<wivrn_foveation> foveation;

	using base = comp_target;
	wivrn_comp_target(wivrn::wivrn_session & cnx, struct comp_compositor * c);
	~wivrn_comp_target();

	void on_feedback(const from_headset::feedback &, const clock_offset &);
	void reset_encoders();

	float get_requested_refresh_rate() const;
	void reset_requested_refresh_rate();

	void pause();
	void resume();

	void set_bitrate(uint32_t bitrate_bps);
	void set_refresh_rate(float);
	float get_refresh_rate();
};

inline float get_default_rate(const from_headset::headset_info_packet & info, const from_headset::settings_changed & settings)
{
	if (settings.preferred_refresh_rate)
		return settings.preferred_refresh_rate;
	return info.available_refresh_rates.back();
}

} // namespace wivrn
