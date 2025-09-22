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

#include "utils/singleton.h"
#include <mutex>
#include <vulkan/vulkan_raii.hpp>

struct render_resources;

namespace wivrn
{

struct clock_offset;
struct wivrn_vk_bundle;

class wivrn_foveation : public singleton<wivrn_foveation>
{
	std::mutex mutex;

	const size_t foveated_width; // per eye
	const size_t foveated_height;

	// Natural vertical gaze angle
	const float angle_offset;
	// Optionally defined from environment variables
	const float convergence_distance;

	float eye_x[2] = {}; // eye x position
	xrt_quat gaze = {};
	from_headset::override_foveation_center manual_foveation = {};
	std::array<to_headset::foveation_parameter, 2> params;

	vk::raii::CommandPool command_pool;
	vk::raii::CommandBuffer cmd;
	buffer_allocation gpu_buffer;
	buffer_allocation host_buffer;

	// parameters used for last computation
	struct P
	{
		xrt_quat gaze = {};
		bool flip_y = false;
		xrt_rect src[2] = {};
		xrt_fov fovs[2] = {};
		float eye_x[2] = {};

		from_headset::override_foveation_center manual_foveation = {};
	};
	P last;

	// must hold lock on mutex to call it
	void compute_params(
	        xrt_rect src[2],
	        const xrt_fov fovs[2]);

public:
	wivrn_foveation(wivrn_vk_bundle &, const xrt_hmd_parts &);

	void update_tracking(const from_headset::tracking &, const clock_offset &);
	void update_foveation_center_override(const from_headset::override_foveation_center &);
	std::array<to_headset::foveation_parameter, 2> get_parameters();

	vk::Buffer get_gpu_buffer();

	vk::CommandBuffer update_foveation_buffer(
	        vk::Buffer target,
	        bool flip_y,
	        xrt_rect src[2],
	        xrt_fov fovs[2]);
};
} // namespace wivrn
