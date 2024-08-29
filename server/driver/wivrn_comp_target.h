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
	enum class status_t
	{
		free,
		acquired,
		encoding,
	};
	struct item
	{
		image_allocation image;
		vk::raii::ImageView image_view = nullptr;
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
	yuv_converter yuv;
	int64_t frame_index;
	to_headset::video_stream_data_shard::view_info_t view_info{};
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

	std::vector<encoder_settings> settings;
	to_headset::video_stream_description desc{};
	std::list<std::jthread> encoder_threads;
	std::vector<std::shared_ptr<VideoEncoder>> encoders;

	std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx;
	std::unique_ptr<wivrn_foveation_renderer> foveation_renderer = nullptr;

	wivrn_comp_target(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx, struct comp_compositor * c, float fps);
	~wivrn_comp_target();

	void on_feedback(const from_headset::feedback &, const clock_offset &);
	void reset_encoders();

	void render_dynamic_foveation(std::array<to_headset::foveation_parameter, 2> foveation);
};

} // namespace xrt::drivers::wivrn
