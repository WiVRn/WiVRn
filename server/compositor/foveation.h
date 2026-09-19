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

#include <mutex>
#include <vulkan/vulkan_raii.hpp>

struct render_resources;

namespace wivrn
{

struct clock_offset;
struct vk_bundle;

class foveation
{
	std::mutex mutex;

	const vk::Extent3D foveated_size; // per eye
	std::array<XrVector2f, 2> foveation_angles = {};
	std::array<to_headset::foveation_parameter, 2> params;

	buffer_allocation gpu_buffer;
	vk::raii::Sampler sampler;

	vk::raii::DescriptorSetLayout ds_layout;
	vk::raii::PipelineLayout layout;
	std::array<vk::raii::Pipeline, 2> pipeline;
	vk::raii::DescriptorPool descriptor_pool;
	vk::DescriptorSet descriptor_set;

	// parameters used for last computation
	struct P
	{
		bool flip_y = false;
		xrt_rect src[2] = {};
		xrt_fov fovs[2] = {};
		std::array<XrVector2f, 2> foveation_angles = {};
	};
	P last;

	// must hold lock on mutex to call it
	void compute_params();

	void update_ubo(
	        vk::raii::CommandBuffer & cmd,
	        bool flip_y,
	        std::array<xrt_rect, 2> src_rect,
	        std::array<xrt_fov, 2> src_fov);

public:
	foveation(wivrn::vk_bundle &, vk::Extent3D foveated_size);

	void update_tracking(const from_headset::tracking &);

	std::array<to_headset::foveation_parameter, 2> foveate(
	        vk::raii::Device &,
	        vk::raii::CommandBuffer & cmd,
	        vk::ImageView y,
	        vk::ImageView cbcr,
	        bool flip_y,
	        std::array<vk::ImageView, 2> src,
	        std::array<xrt_rect, 2> src_rect,
	        std::array<xrt_fov, 2> src_fov,
	        bool alpha);
};
} // namespace wivrn
