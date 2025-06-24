/*
 * WiVRn VR streaming
 * Copyright (C) 2024  galister <galister@librevr.org>
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "vk/allocation.h"
#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include <vulkan/vulkan_raii.hpp>

struct render_resources;

namespace wivrn
{

struct clock_offset;
struct wivrn_vk_bundle;

class wivrn_foveation
{
	std::mutex mutex;

	const size_t foveated_width; // per eye
	const size_t foveated_height;

	std::array<from_headset::tracking::view, 2> views = {};
	xrt_quat gaze = {};
	std::array<to_headset::foveation_parameter, 2> params;

	vk::raii::CommandPool command_pool;
	vk::raii::CommandBuffer cmd;
	buffer_allocation host_buffer;
	vk::Buffer gpu_buffer = nullptr;

	// parameters used for last computation
	struct P
	{
		xrt_quat gaze = {};
		bool flip_y = false;
		xrt_rect src[2] = {};
		xrt_fov fovs[2] = {};
		std::array<from_headset::tracking::view, 2> views = {};
	};
	P last;

	// must hold lock on mutex to call it
	void compute_params(
	        xrt_rect src[2],
	        const xrt_fov fovs[2]);

public:
	wivrn_foveation(wivrn_vk_bundle &, const xrt_hmd_parts &);

	void update_tracking(const from_headset::tracking &, const clock_offset &);
	std::array<to_headset::foveation_parameter, 2> get_parameters();

	vk::CommandBuffer update_foveation_buffer(
	        vk::Buffer target,
	        bool flip_y,
	        xrt_rect src[2],
	        xrt_fov fovs[2]);
};
} // namespace wivrn
