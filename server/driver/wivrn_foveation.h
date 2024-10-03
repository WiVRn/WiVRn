/*
 * WiVRn VR streaming
 * Copyright (C) 2024  galister <galister@librevr.org>
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
#include "xrt/xrt_defines.h"

#include <vulkan/vulkan_raii.hpp>

namespace wivrn
{

struct clock_offset;
struct wivrn_vk_bundle;

// Calculates the center parameter from the received eye tracking data.
class wivrn_foveation
{
	std::mutex mutex;

	std::array<xrt_vec2, 2> center_offset = {};
	std::array<from_headset::tracking::view, 2> views = {};
	xrt_quat last_gaze = {};

public:
	wivrn_foveation() {}

	void set_initial_parameters(std::array<to_headset::foveation_parameter, 2> p);
	void update_tracking(const from_headset::tracking &, const clock_offset &);
	std::array<xrt_vec2, 2> get_center();
};

// Renders foveation parameters to Monado's distortion images using a compute shader
class wivrn_foveation_renderer
{
	wivrn_vk_bundle & vk;

	vk::raii::DescriptorPool dp = nullptr;
	vk::raii::DescriptorSetLayout ds_layout = nullptr;
	vk::raii::PipelineLayout layout = nullptr;
	vk::raii::Pipeline pipeline = nullptr;
	std::array<vk::DescriptorSet, 2> ds;

public:
	wivrn_foveation_renderer(wivrn_vk_bundle & vk, vk::raii::CommandPool & cmd_pool);

	vk::raii::CommandBuffer cmd_buf = nullptr;

	void render_distortion_images(std::array<to_headset::foveation_parameter, 2> foveation_arr, const VkImage * images, const VkImageView * image_views);
};
} // namespace wivrn
