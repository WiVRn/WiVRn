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

// uses code borrowed from Monado with the following copyright notice:
// Copyright 2019-2024, Collabora, Ltd.
// Copyright 2025, NVIDIA CORPORATION.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Compositor (compute shader) rendering code.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @author Christoph Haag <christoph.haag@collabora.com>
 * @author Fernando Velazquez Innella <finnella@magicleap.com>
 * @author Rylie Pavlik <rylie.pavlik@collabora.com>
 * @ingroup comp_util
 */

#include "render/render_interface.h"

#include "layer_squasher.h"

#include "driver/wivrn_hmd.h"
#include "utils/wivrn_vk_bundle.h"
#include "vk/specialization_constants.h"

#include "main/comp_compositor.h"
#include "math/m_space.h"
#include "shaders/layer_defines.inc.glsl"
#include "util/comp_layer_accum.h"
#include "util/comp_render_helpers.h"

#include <array>
#include <format>
#include <ranges>

namespace
{
const uint32_t view_count = 2;
const bool do_timewarp = true;

const std::array formats{
        vk::Format::eR8G8B8A8Srgb,
        vk::Format::eR8G8B8A8Unorm,
};

const comp_layer *
get_projection_layer(const comp_layer_accum & layers)
{
	for (const auto & layer: std::span(layers.layers, layers.layer_count))
		switch (layer.data.type)
		{
			case XRT_LAYER_PROJECTION:
			case XRT_LAYER_PROJECTION_DEPTH:
				return &layer;
			case XRT_LAYER_QUAD:
			case XRT_LAYER_CUBE:
			case XRT_LAYER_CYLINDER:
			case XRT_LAYER_EQUIRECT1:
			case XRT_LAYER_EQUIRECT2:
			case XRT_LAYER_PASSTHROUGH:
				break;
		}
	return NULL;
}
struct pose_data
{
	std::array<xrt_fov, XRT_MAX_VIEWS> fovs;
	std::array<xrt_pose, XRT_MAX_VIEWS> world_poses;
	std::array<xrt_pose, XRT_MAX_VIEWS> eye_poses;
	pose_data(
	        wivrn::wivrn_hmd & hmd,
	        uint64_t frame_interval_ns,
	        const comp_frame & frame,
	        const comp_layer_accum & layers)
	{
		// Get pose data from projection layer so we avoid one resampling step
		const auto * proj = get_projection_layer(layers);
		// Except if it's not for the right frame, as it would make quads stutter
		int64_t predicted_display_time_ns = frame.predicted_display_time_ns;
		int64_t cutoff_ns = 3 * frame_interval_ns;
		if (proj and llabs(predicted_display_time_ns - proj->data.timestamp) <= cutoff_ns)
		{
			// depth has the same first member as proj
			for (auto [i, data]: std::ranges::enumerate_view(proj->data.proj.v))
			{
				fovs[i] = data.fov;
				world_poses[i] = data.pose;
				eye_poses[i] = data.pose;
			}
		}
		else
		{
			xrt_space_relation head_rel;
			hmd.get_view_poses(
			        nullptr, // unused
			        frame.predicted_display_time_ns,
			        XRT_VIEW_TYPE_STEREO,
			        view_count,
			        &head_rel,
			        fovs.data(),
			        eye_poses.data());
			for (uint32_t i = 0; i < view_count; ++i)
			{
				xrt_relation_chain xrc{};
				xrt_space_relation rel = XRT_SPACE_RELATION_ZERO;

				m_relation_chain_push_pose_if_not_identity(&xrc, &eye_poses[i]);
				m_relation_chain_push_relation(&xrc, &head_rel);
				m_relation_chain_resolve(&xrc, &rel);
				world_poses[i] = rel.pose;
			}
		}
	}
};

const comp_swapchain_image & get_layer_image(const comp_layer & layer, uint32_t swapchain_index, uint32_t image_index)
{
	return reinterpret_cast<struct comp_swapchain *>(comp_layer_get_swapchain(&layer, swapchain_index))->images[image_index];
}

const struct comp_swapchain_image &
get_layer_depth_image(const comp_layer & layer, uint32_t swapchain_index, uint32_t image_index)
{
	return reinterpret_cast<struct comp_swapchain *>(comp_layer_get_depth_swapchain(&layer, swapchain_index))->images[image_index];
}

uint32_t
uint_divide_and_round_up(uint32_t a, uint32_t b)
{
	return (a + (b - 1)) / b;
}

std::array<uint32_t, 2> calc_dispatch_dims_1_view(const render_viewport_data & views)
{
	// Power of two divide and round up.
	return {
	        uint_divide_and_round_up(views.w, 8),
	        uint_divide_and_round_up(views.h, 8),
	};
}

buffer_allocation make_ubo(vk::raii::Device & device, int i)
{
	return buffer_allocation{
	        device,
	        vk::BufferCreateInfo{
	                .size = sizeof(render_compute_layer_ubo_data),
	                .usage = vk::BufferUsageFlagBits::eUniformBuffer,
	        },
	        VmaAllocationCreateInfo{
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        std::format("layer squasher UBO {}", i)};
}

vk::raii::ImageView make_view(wivrn::vk_bundle & vk, image_allocation & image, vk::ImageUsageFlagBits flag, uint32_t view)
{
	vk::raii::ImageView res{vk.device,
	                        vk::StructureChain{
	                                vk::ImageViewCreateInfo{
	                                        .image = image,
	                                        .viewType = vk::ImageViewType::e2D,
	                                        .format = flag == vk::ImageUsageFlagBits::eSampled ? formats[0] : formats[1],
	                                        .subresourceRange = {
	                                                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                                                .levelCount = 1,
	                                                .baseArrayLayer = view,
	                                                .layerCount = 1,
	                                        },
	                                },
	                                vk::ImageViewUsageCreateInfo{
	                                        .usage = flag,
	                                },

	                        }
	                                .get()};
	vk.name(*res, std::format("layer squasher view {}", view).c_str());
	return res;
}

vk::raii::DescriptorSetLayout make_ds_layout(wivrn::vk_bundle & vk, uint32_t image_array_size, bool partially_bound_desc)
{
	std::array bindings{
	        vk::DescriptorSetLayoutBinding{
	                .binding = 0,
	                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = image_array_size,
	                .stageFlags = vk::ShaderStageFlagBits::eCompute,
	        },
	        vk::DescriptorSetLayoutBinding{
	                .binding = 2,
	                .descriptorType = vk::DescriptorType::eStorageImage,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eCompute,
	        },
	        vk::DescriptorSetLayoutBinding{
	                .binding = 3,
	                .descriptorType = vk::DescriptorType::eUniformBuffer,
	                .descriptorCount = 1,
	                .stageFlags = vk::ShaderStageFlagBits::eCompute,
	        },
	};
	std::array<vk::DescriptorBindingFlags, std::size(bindings)> flags{
	        vk::DescriptorBindingFlagBits::ePartiallyBound,
	        vk::DescriptorBindingFlagBits::ePartiallyBound,
	        vk::DescriptorBindingFlags{},
	};

	vk::StructureChain info{
	        vk::DescriptorSetLayoutCreateInfo{
	                .bindingCount = bindings.size(),
	                .pBindings = bindings.data(),
	        },
	        vk::DescriptorSetLayoutBindingFlagsCreateInfo{
	                .bindingCount = flags.size(),
	                .pBindingFlags = flags.data(),
	        }};

	if (not partially_bound_desc)
		info.unlink<vk::DescriptorSetLayoutBindingFlagsCreateInfo>();

	vk::raii::DescriptorSetLayout res(vk.device, info.get());
	vk.name(*res, "layer squasher descriptor set layout");
	return res;
}
vk::raii::PipelineLayout make_layout(wivrn::vk_bundle & vk, vk::DescriptorSetLayout ds_layout)
{
	vk::raii::PipelineLayout res(vk.device,
	                             vk::PipelineLayoutCreateInfo{
	                                     .setLayoutCount = 1,
	                                     .pSetLayouts = &ds_layout,
	                             });
	vk.name(*res, "layer squasher pipeline layout");
	return res;
}

vk::raii::Pipeline make_pipeline(wivrn::vk_bundle & vk, uint32_t image_array_size, vk::PipelineLayout layout)
{
	auto specialization = make_specialization_constants(
	        int32_t(0),     // unused
	        VkBool32(true), // do_timewarp
	        VkBool32(true), // do_color_correction
	        int32_t(image_array_size));
	vk::raii::Pipeline res(
	        vk.device,
	        nullptr,
	        vk::ComputePipelineCreateInfo{
	                .stage = {
	                        .stage = vk::ShaderStageFlagBits::eCompute,
	                        .module = *vk.load_shader("layer"),
	                        .pName = "main",
	                        .pSpecializationInfo = specialization,
	                },
	                .layout = layout,
	        });
	vk.name(*res, "layer squasher pipeline");
	return res;
}

vk::raii::DescriptorPool make_ds_pool(wivrn::vk_bundle & vk, uint32_t image_array_size)
{
	const uint32_t view_size = 2;
	std::array pool_sizes{
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = view_size * image_array_size,
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageImage,
	                .descriptorCount = view_size,
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eUniformBuffer,
	                .descriptorCount = view_size,
	        },
	};
	vk::raii::DescriptorPool res{
	        vk.device,
	        vk::DescriptorPoolCreateInfo{
	                .maxSets = view_size,
	                .poolSizeCount = pool_sizes.size(),
	                .pPoolSizes = pool_sizes.data(),
	        },
	};
	vk.name(*res, "layer squasher descriptor pool");
	return res;
}

} // namespace

namespace wivrn
{

layer_squasher::layer_squasher(vk_bundle & vk, vk::Extent3D target_size) :
        partially_bound_desc{bool(std::get<vk::PhysicalDeviceVulkan12Features>(vk.feat).descriptorBindingPartiallyBound)},
        image_array_size{std::min<uint32_t>(vk.physical_device.getProperties().limits.maxPerStageDescriptorSampledImages, RENDER_MAX_IMAGES_SIZE)},
        clamp_to_border_black{vk.device,
                              vk::SamplerCreateInfo{
                                      .magFilter = vk::Filter::eLinear,
                                      .minFilter = vk::Filter::eLinear,
                                      .mipmapMode = vk::SamplerMipmapMode::eLinear,
                                      .addressModeU = vk::SamplerAddressMode::eClampToBorder,
                                      .addressModeV = vk::SamplerAddressMode::eClampToBorder,
                                      .addressModeW = vk::SamplerAddressMode::eClampToBorder,
                                      .borderColor = vk::BorderColor::eFloatOpaqueBlack,
                              }},
        clamp_to_edge{vk.device,
                      vk::SamplerCreateInfo{
                              .magFilter = vk::Filter::eLinear,
                              .minFilter = vk::Filter::eLinear,
                              .mipmapMode = vk::SamplerMipmapMode::eLinear,
                              .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                              .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                              .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                              .borderColor = vk::BorderColor::eFloatOpaqueBlack,
                      }},
        ubo{make_ubo(vk.device, 0), make_ubo(vk.device, 1)},
        render_target{vk.device,
                      vk::StructureChain{
                              vk::ImageCreateInfo{
                                      .flags = vk::ImageCreateFlagBits::eExtendedUsage | vk::ImageCreateFlagBits::eMutableFormat,
                                      .imageType = vk::ImageType::e2D,
                                      .format = formats[0],
                                      .extent = target_size,
                                      .mipLevels = 1,
                                      .arrayLayers = view_count,
                                      .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                              },
                              vk::ImageFormatListCreateInfo{
                                      .viewFormatCount = formats.size(),
                                      .pViewFormats = formats.data(),
                              },

                      }
                              .get(),
                      VmaAllocationCreateInfo{},
                      "layer squasher target"},
        render_view_srgb{make_view(vk, render_target, vk::ImageUsageFlagBits::eSampled, 0), make_view(vk, render_target, vk::ImageUsageFlagBits::eSampled, 1)},
        render_view_unorm{make_view(vk, render_target, vk::ImageUsageFlagBits::eStorage, 0), make_view(vk, render_target, vk::ImageUsageFlagBits::eStorage, 1)},
        ds_layout{make_ds_layout(vk, image_array_size, partially_bound_desc)},
        layout{make_layout(vk, *ds_layout)},
        pipeline{make_pipeline(vk, image_array_size, *layout)},
        descriptor_pool{make_ds_pool(vk, image_array_size)}
{
	{
		std::array layouts = {*ds_layout, *ds_layout};
		auto sets = vk.device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{
		        .descriptorPool = descriptor_pool,
		        .descriptorSetCount = uint32_t(descriptor_sets.size()),
		        .pSetLayouts = layouts.data(),
		});
		for (int i = 0; i < view_count; ++i)
			descriptor_sets[i] = sets[i].release();
	}
}

std::tuple<std::array<xrt_pose, 2>,
           std::array<xrt_fov, 2>,
           std::array<xrt_rect, 2>>
layer_squasher::do_layers(
        vk::raii::Device & device,
        vk::raii::CommandBuffer & cmd,
        wivrn_hmd & hmd,
        uint64_t frame_interval_ns,
        const comp_frame & frame,
        const comp_layer_accum & layers)
{
	// get the head/pose to reproject to
	const pose_data poses{hmd, frame_interval_ns, frame, layers};
	const auto extent3D = render_target.info().extent;
	std::array viewports{
	        render_viewport_data{
	                .w = extent3D.width,
	                .h = extent3D.height,
	        },
	        render_viewport_data{
	                .w = extent3D.width,
	                .h = extent3D.height,
	        },
	};

	vk::ImageMemoryBarrier im_barrier{
	        .dstAccessMask = vk::AccessFlagBits::eShaderWrite,
	        .oldLayout = vk::ImageLayout::eUndefined,
	        .newLayout = vk::ImageLayout::eGeneral,
	        .image = render_target,
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .levelCount = 1,
	                .layerCount = vk::RemainingArrayLayers,
	        },
	};

	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eAllCommands,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        {},
	        {},
	        im_barrier);

	for (int view = 0; view < view_count; ++view)
	{
		// TODO: staging buffer?
		auto & ubo = *(render_compute_layer_ubo_data *)this->ubo[view].map();

		ubo.view = viewports[view];
		render_calc_uv_to_tangent_lengths_rect(&poses.fovs[view], &ubo.pre_transform);

		// Not the transform of the views, but the inverse: actual view matrices.
		xrt_matrix_4x4 world_view_mat, eye_view_mat;
		math_matrix_4x4_view_from_pose(&poses.world_poses[view], &world_view_mat);
		math_matrix_4x4_view_from_pose(&poses.eye_poses[view], &eye_view_mat);

		std::array<vk::DescriptorImageInfo, RENDER_MAX_IMAGES_SIZE> src_image_info{};
		for (auto & src: src_image_info)
			src.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		uint32_t cur_image = 0;
		int cur_layer = 0;
		for (const auto & layer: std::span(layers.layers, layers.layer_count))
		{
			if (not is_layer_view_visible(&layer.data, view))
				continue;

			const auto & data = layer.data;

			/*!
			 * Stop compositing layers if device's sampled image limit is
			 * reached. For most hardware this isn't a problem, most have
			 * well over 32 max samplers. But notably the RPi4 only have 16
			 * which is a limit we may run into. But if you got 16+ layers
			 * on a RPi4 you have more problems then max samplers.
			 */
			uint32_t required_image_samplers;
			switch (data.type)
			{
				case XRT_LAYER_CYLINDER:
				case XRT_LAYER_EQUIRECT2:
				case XRT_LAYER_PROJECTION:
				case XRT_LAYER_QUAD:
					required_image_samplers = 1;
					break;
				case XRT_LAYER_PROJECTION_DEPTH:
					required_image_samplers = 2;
					break;
				default:
					U_LOG_E("Skipping layer %ld, unknown type: %u", std::distance(layers.layers, &layer), data.type);
					continue; // Skip this layer if don't know about it.
			}

			//! Exit loop if shader cannot receive more image samplers
			if (cur_image + required_image_samplers > image_array_size)
				break;

			switch (data.type)
			{
				case XRT_LAYER_PROJECTION:
				case XRT_LAYER_PROJECTION_DEPTH:
					do_projection_layer(
					        layer,
					        poses.world_poses[view],
					        view,
					        cur_layer,
					        cur_image,
					        src_image_info,
					        ubo);
					break;
				case XRT_LAYER_QUAD:
					do_quad_layer(
					        layer,
					        eye_view_mat,
					        world_view_mat,
					        view,
					        cur_layer,
					        cur_image,
					        src_image_info,
					        ubo);
					break;
				case XRT_LAYER_CYLINDER:
					do_cylinder_layer(
					        layer,
					        eye_view_mat,
					        world_view_mat,
					        view,
					        cur_layer,
					        cur_image,
					        src_image_info,
					        ubo);
					break;
				case XRT_LAYER_EQUIRECT2:
					do_equirect2_layer(
					        layer,
					        eye_view_mat,
					        world_view_mat,
					        view,
					        cur_layer,
					        cur_image,
					        src_image_info,
					        ubo);
					break;
				case XRT_LAYER_EQUIRECT1:
				case XRT_LAYER_PASSTHROUGH:
				case XRT_LAYER_CUBE:
					U_LOG_E("unsupported layer type");
					assert(false);
					continue;
			}
			ubo.layers[cur_layer].layer_data.unpremultiplied_alpha = is_layer_unpremultiplied(&layer.data);
			apply_bias_and_scale_from_layer(
			        &data,
			        &ubo.layers[cur_layer].color_scale,
			        &ubo.layers[cur_layer].color_bias);
			cur_layer++;
		}
		ubo.layer_count.value = cur_layer;

		for (auto & layer: std::span(ubo.layers).subspan(cur_layer))
			layer.layer_data.layer_type = LAYER_COMP_TYPE_NOOP;

		vk::DescriptorImageInfo target_image_info{
		        .imageView = *render_view_unorm[view],
		        .imageLayout = vk::ImageLayout::eGeneral,
		};

		vk::DescriptorBufferInfo ubo_info{
		        .buffer = this->ubo[view],
		        .range = vk::WholeSize,
		};

		std::array writes = {
		        vk::WriteDescriptorSet{
		                .dstSet = descriptor_sets[view],
		                .dstBinding = 0,
		                .descriptorCount = uint32_t(cur_image),
		                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                .pImageInfo = src_image_info.data(),
		        },
		        vk::WriteDescriptorSet{
		                .dstSet = descriptor_sets[view],
		                .dstBinding = 2,
		                .descriptorCount = 1,
		                .descriptorType = vk::DescriptorType::eStorageImage,
		                .pImageInfo = &target_image_info,
		        },
		        vk::WriteDescriptorSet{
		                .dstSet = descriptor_sets[view],
		                .dstBinding = 3,
		                .descriptorCount = 1,
		                .descriptorType = vk::DescriptorType::eUniformBuffer,
		                .pBufferInfo = &ubo_info,
		        },
		};

		if (not partially_bound_desc)
		{
			for (int i = cur_image; i < image_array_size; ++i)
				src_image_info[i] = src_image_info[0];
			writes[0].descriptorCount = image_array_size;
		}

		device.updateDescriptorSets(writes, {});

		cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *layout, 0, descriptor_sets[view], {});
		auto [w, h] = calc_dispatch_dims_1_view(viewports[view]);
		cmd.dispatch(w, h, 1);
	}

	im_barrier.oldLayout = im_barrier.newLayout;
	im_barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	im_barrier.srcAccessMask = im_barrier.dstAccessMask;
	im_barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        {},
	        {},
	        im_barrier);

	std::array<xrt_rect, 2> rect;
	for (auto [v, r]: std::ranges::zip_view(viewports, rect))
		r = {.extent = {.w = int(v.w), .h = int(v.h)}};

	return {poses.world_poses, poses.fovs, rect};
}

std::array<vk::ImageView, 2> layer_squasher::get_views()
{
	return {*render_view_srgb[0], *render_view_srgb[1]};
}

uint32_t layer_squasher::max_layers(const vk::PhysicalDeviceProperties & prop) const
{
	// We may be limited by samplers, and use up to 2 samplers per layer
	return std::min({XRT_MAX_LAYERS * 2u,
	                 prop.limits.maxPerStageDescriptorSampledImages,
	                 prop.limits.maxPerStageDescriptorSamplers}) /
	       2u;
}

void layer_squasher::do_projection_layer(
        const comp_layer & layer,
        const xrt_pose & world_pose,
        int view,
        int cur_layer,
        uint32_t & cur_image,
        std::span<vk::DescriptorImageInfo> src_image_info,
        render_compute_layer_ubo_data & ubo)
{
	const xrt_layer_data & layer_data = layer.data;
	const auto & vd = layer_data.type == XRT_LAYER_PROJECTION ? layer_data.proj.v[view] : layer_data.depth.v[view];

	uint32_t sc_array_index = view % 2;

	ubo.layers[cur_layer].layer_data.layer_type = LAYER_COMP_TYPE_PROJECTION;

	// Color
	src_image_info[cur_image].sampler = *clamp_to_border_black;
	src_image_info[cur_image].imageView = get_image_view(
	        &get_layer_image(layer, sc_array_index, vd.sub.image_index),
	        layer_data.flags,
	        vd.sub.array_index);
	ubo.layers[cur_layer].image_info.color_image_index = cur_image++;

	// Depth
	if (layer_data.type == XRT_LAYER_PROJECTION_DEPTH)
	{
		const auto & dvd = layer_data.depth.d[view];

		src_image_info[cur_image].sampler = *clamp_to_edge; // Edge to keep depth stable at edges.
		src_image_info[cur_image].imageView = get_image_view(
		        &get_layer_depth_image(layer, sc_array_index, dvd.sub.image_index),
		        layer_data.flags,
		        dvd.sub.array_index);
		ubo.layers[cur_layer].image_info.depth_image_index = cur_image++;
	}

	set_post_transform_rect(
	        &layer_data,
	        &vd.sub.norm_rect,
	        false,
	        &ubo.layers[cur_layer].post_transforms);

	// unused if timewarp is off
	if (do_timewarp)
		render_calc_time_warp_matrix(
		        &vd.pose,
		        &vd.fov,
		        &world_pose,
		        &ubo.layers[cur_layer].transforms_timewarp);
}

void layer_squasher::do_quad_layer(const comp_layer & layer,
                                   const xrt_matrix_4x4 & eye_view_mat,
                                   const xrt_matrix_4x4 & world_view_mat,
                                   uint32_t view_index,
                                   int cur_layer,
                                   uint32_t & cur_image,
                                   std::span<vk::DescriptorImageInfo> src_image_info,
                                   render_compute_layer_ubo_data & ubo)
{
	const xrt_layer_data & layer_data = layer.data;
	const xrt_layer_quad_data & q = layer_data.quad;

	ubo.layers[cur_layer].layer_data.layer_type = LAYER_COMP_TYPE_QUAD;

	// Image to use.
	src_image_info[cur_image].sampler = *clamp_to_edge;
	src_image_info[cur_image].imageView = get_image_view(
	        &get_layer_image(layer, 0, q.sub.image_index),
	        layer_data.flags,
	        q.sub.array_index);

	// Set the normalized post transform values.
	xrt_normalized_rect post_transform{};

	// Used for Subimage and OpenGL flip.
	set_post_transform_rect(
	        &layer_data,
	        &q.sub.norm_rect,
	        true,
	        &post_transform);

	// Is this layer viewspace or not.
	const xrt_matrix_4x4 & view_mat = is_layer_view_space(&layer_data) ? eye_view_mat : world_view_mat;

	// Transform quad pose into view space.
	xrt_vec3 quad_position{};
	math_matrix_4x4_transform_vec3(&view_mat, &layer_data.quad.pose.position, &quad_position);

	// neutral quad layer faces +z, towards the user
	xrt_vec3 normal{.x = 0, .y = 0, .z = 1};

	// rotation of the quad normal in world space
	math_quat_rotate_vec3(&layer_data.quad.pose.orientation, &normal, &normal);

	/*
	 * normal is a vector that originates on the plane, not on the origin.
	 * Instead of using the inverse quad transform to transform it into view space we can
	 * simply add up vectors:
	 *
	 * combined_normal [in world space] = plane_origin [in world space] + normal [in plane
	 * space] [with plane in world space]
	 *
	 * Then combined_normal can be transformed to view space via view matrix and a new
	 * normal_view_space retrieved:
	 *
	 * normal_view_space = combined_normal [in view space] - plane_origin [in view space]
	 */
	xrt_vec3 normal_view_space = normal;
	math_vec3_accum(&layer_data.quad.pose.position, &normal_view_space);
	math_matrix_4x4_transform_vec3(&view_mat, &normal_view_space, &normal_view_space);
	math_vec3_subtract(&quad_position, &normal_view_space);

	xrt_vec3 scale{1.f, 1.f, 1.f};
	xrt_matrix_4x4 plane_transform_view_space, inverse_quad_transform;
	math_matrix_4x4_model(&layer_data.quad.pose, &scale, &plane_transform_view_space);
	math_matrix_4x4_multiply(&view_mat, &plane_transform_view_space, &plane_transform_view_space);
	math_matrix_4x4_inverse(&plane_transform_view_space, &inverse_quad_transform);

	// Write all of the UBO data.
	ubo.layers[cur_layer].post_transforms = post_transform;
	ubo.layers[cur_layer].quad_extent.val = layer_data.quad.size;
	ubo.layers[cur_layer].quad_position.val = quad_position;
	ubo.layers[cur_layer].quad_normal.val = normal_view_space;
	ubo.layers[cur_layer].inverse_quad_transform = inverse_quad_transform;
	ubo.layers[cur_layer].image_info.color_image_index = cur_image;
	cur_image++;
}

void layer_squasher::do_cylinder_layer(const comp_layer & layer,
                                       const xrt_matrix_4x4 & eye_view_mat,
                                       const xrt_matrix_4x4 & world_view_mat,
                                       uint32_t view_index,
                                       int cur_layer,
                                       uint32_t & cur_image,
                                       std::span<vk::DescriptorImageInfo> src_image_info,
                                       render_compute_layer_ubo_data & ubo)
{
	const xrt_layer_data & layer_data = layer.data;
	const xrt_layer_cylinder_data & c = layer_data.cylinder;

	ubo.layers[cur_layer].layer_data.layer_type = LAYER_COMP_TYPE_CYLINDER;

	// Image to use.
	src_image_info[cur_image].sampler = *clamp_to_edge;
	src_image_info[cur_image].imageView = get_image_view(
	        &get_layer_image(layer, 0, c.sub.image_index),
	        layer_data.flags,
	        c.sub.array_index);

	// Used for Subimage and OpenGL flip.
	set_post_transform_rect(
	        &layer_data,
	        &c.sub.norm_rect,
	        false,
	        &ubo.layers[cur_layer].post_transforms);

	ubo.layers[cur_layer].cylinder_data.central_angle = c.central_angle;
	ubo.layers[cur_layer].cylinder_data.aspect_ratio = c.aspect_ratio;

	xrt_vec3 scale{1.f, 1.f, 1.f};

	xrt_matrix_4x4 model;
	math_matrix_4x4_model(&c.pose, &scale, &model);

	xrt_matrix_4x4 model_inv;
	math_matrix_4x4_inverse(&model, &model_inv);

	const xrt_matrix_4x4 & v = is_layer_view_space(&layer_data) ? eye_view_mat : world_view_mat;

	xrt_matrix_4x4 v_inv;
	math_matrix_4x4_inverse(&v, &v_inv);

	math_matrix_4x4_multiply(&model_inv, &v_inv, &ubo.layers[cur_layer].mv_inverse);

	// Simplifies the shader.
	ubo.layers[cur_layer].cylinder_data.radius = c.radius >= INFINITY ? 0.f : c.radius;

	ubo.layers[cur_layer].cylinder_data.central_angle = c.central_angle;
	ubo.layers[cur_layer].cylinder_data.aspect_ratio = c.aspect_ratio;

	ubo.layers[cur_layer].image_info.color_image_index = cur_image;
	cur_image++;
}

void layer_squasher::do_equirect2_layer(const comp_layer & layer,
                                        const xrt_matrix_4x4 & eye_view_mat,
                                        const xrt_matrix_4x4 & world_view_mat,
                                        uint32_t view_index,
                                        int cur_layer,
                                        uint32_t & cur_image,
                                        std::span<vk::DescriptorImageInfo> src_image_info,
                                        render_compute_layer_ubo_data & ubo)
{
	const xrt_layer_data & layer_data = layer.data;
	const xrt_layer_equirect2_data & eq2 = layer_data.equirect2;

	ubo.layers[cur_layer].layer_data.layer_type = LAYER_COMP_TYPE_EQUIRECT2;

	// Image to use.
	src_image_info[cur_image].sampler = *clamp_to_edge;
	src_image_info[cur_image].imageView = get_image_view(
	        &get_layer_image(layer, 0, eq2.sub.image_index),
	        layer_data.flags,
	        eq2.sub.array_index);

	// Used for Subimage and OpenGL flip.
	set_post_transform_rect(                         //
	        &layer_data,                             // data
	        &eq2.sub.norm_rect,                      // src_norm_rect
	        false,                                   // invert_flip
	        &ubo.layers[cur_layer].post_transforms); // out_norm_rect

	xrt_vec3 scale{1.f, 1.f, 1.f};

	xrt_matrix_4x4 model;
	math_matrix_4x4_model(&eq2.pose, &scale, &model);

	xrt_matrix_4x4 model_inv;
	math_matrix_4x4_inverse(&model, &model_inv);

	const xrt_matrix_4x4 & v = is_layer_view_space(&layer_data) ? eye_view_mat : world_view_mat;

	xrt_matrix_4x4 v_inv;
	math_matrix_4x4_inverse(&v, &v_inv);

	math_matrix_4x4_multiply(&model_inv, &v_inv, &ubo.layers[cur_layer].mv_inverse);

	// Simplifies the shader.
	ubo.layers[cur_layer].eq2_data.radius = eq2.radius >= INFINITY ? 0.f : eq2.radius;

	ubo.layers[cur_layer].eq2_data.central_horizontal_angle = eq2.central_horizontal_angle;
	ubo.layers[cur_layer].eq2_data.upper_vertical_angle = eq2.upper_vertical_angle;
	ubo.layers[cur_layer].eq2_data.lower_vertical_angle = eq2.lower_vertical_angle;

	ubo.layers[cur_layer].image_info.color_image_index = cur_image;
	cur_image++;
}

} // namespace wivrn
