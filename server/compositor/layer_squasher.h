/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "xrt/xrt_defines.h"

#include <vulkan/vulkan_raii.hpp>

struct comp_layer;
struct comp_layer_accum;
struct comp_frame;
struct render_compute_layer_ubo_data;

struct xrt_pose;
struct xrt_matrix_4x4;

namespace wivrn
{
struct vk_bundle;
class wivrn_hmd;
class layer_squasher
{
	const bool partially_bound_desc;
	const uint32_t image_array_size;
	vk::raii::Sampler clamp_to_border_black;
	vk::raii::Sampler clamp_to_edge;
	std::array<buffer_allocation, 2> ubo;
	image_allocation render_target;
	std::array<vk::raii::ImageView, 2> render_view_srgb;
	std::array<vk::raii::ImageView, 2> render_view_unorm;
	vk::raii::DescriptorSetLayout ds_layout;
	vk::raii::PipelineLayout layout;
	vk::raii::Pipeline pipeline;
	vk::raii::DescriptorPool descriptor_pool;
	std::array<vk::DescriptorSet, 2> descriptor_sets;

public:
	layer_squasher(vk_bundle &, vk::Extent3D target_size);

	std::tuple<std::array<xrt_pose, 2>,
	           std::array<xrt_fov, 2>,
	           std::array<xrt_rect, 2>>
	do_layers(
	        vk::raii::Device &,
	        vk::raii::CommandBuffer &,
	        wivrn_hmd &,
	        uint64_t frame_interval_ns,
	        const comp_frame &,
	        const comp_layer_accum &,
	        const xrt_rect & min_size);

	std::array<vk::ImageView, 2> get_views();

	uint32_t max_layers(const vk::PhysicalDeviceProperties &) const;

private:
	xrt_fov do_projection_layer(
	        const comp_layer & layer,
	        const xrt_pose & world_pose,
	        int view,
	        int cur_layer,
	        uint32_t & cur_image,
	        std::span<vk::DescriptorImageInfo> src_image_info,
	        render_compute_layer_ubo_data & ubo);

	xrt_fov do_quad_layer(const comp_layer & layer,
	                      const xrt_matrix_4x4 & eye_view_mat,
	                      const xrt_matrix_4x4 & world_view_mat,
	                      uint32_t view_index,
	                      int cur_layer,
	                      uint32_t & cur_image,
	                      std::span<vk::DescriptorImageInfo> src_image_info,
	                      render_compute_layer_ubo_data & ubo);
	xrt_fov do_cylinder_layer(const comp_layer & layer,
	                          const xrt_matrix_4x4 & eye_view_mat,
	                          const xrt_matrix_4x4 & world_view_mat,
	                          uint32_t view_index,
	                          int cur_layer,
	                          uint32_t & cur_image,
	                          std::span<vk::DescriptorImageInfo> src_image_info,
	                          render_compute_layer_ubo_data & ubo);
	xrt_fov do_equirect2_layer(const comp_layer & layer,
	                           const xrt_matrix_4x4 & eye_view_mat,
	                           const xrt_matrix_4x4 & world_view_mat,
	                           uint32_t view_index,
	                           int cur_layer,
	                           uint32_t & cur_image,
	                           std::span<vk::DescriptorImageInfo> src_image_info,
	                           render_compute_layer_ubo_data & ubo);
};
} // namespace wivrn
