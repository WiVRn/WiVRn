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

#include "render/render_interface.h"

#include "wivrn_foveation.h"

#include "driver/xrt_cast.h"
#include "math/m_api.h"
#include "utils/wivrn_vk_bundle.h"
#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"

#include <array>
#include <cmath>
#include <map>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <openxr/openxr.h>

extern const std::map<std::string, std::vector<uint32_t>> shaders;

namespace wivrn
{

const uint32_t dispatch_group_count = RENDER_DISTORTION_IMAGE_DIMENSIONS / 8;

struct FoveationParamsPcs
{
	float a[2];
	float b[2];
	float scale[2];
	float center[2];
};

static const std::array pool_sizes =
        {
                vk::DescriptorPoolSize{
                        .type = vk::DescriptorType::eStorageImage,
                        .descriptorCount = 2,
                }};

static const std::array<vk::DescriptorSetLayoutBinding, 2> layout_bindings{
        vk::DescriptorSetLayoutBinding{.binding = 0,
                                       .descriptorType = vk::DescriptorType::eStorageImage,
                                       .descriptorCount = 1,
                                       .stageFlags = vk::ShaderStageFlagBits::eCompute},
};

wivrn_foveation_renderer::wivrn_foveation_renderer(wivrn_vk_bundle & vk, vk::raii::CommandPool & cmd_pool) :
        vk(vk),
        dp(vk.device,
           vk::DescriptorPoolCreateInfo{
                   .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                   .maxSets = pool_sizes[0].descriptorCount,
                   .poolSizeCount = pool_sizes.size(),
                   .pPoolSizes = pool_sizes.data(),
           }),
        ds_layout(vk.device, vk::DescriptorSetLayoutCreateInfo{.bindingCount = layout_bindings.size(), .pBindings = layout_bindings.data()})
{
	cmd_buf = std::move(vk.device.allocateCommandBuffers(
	        {.commandPool = *cmd_pool,
	         .commandBufferCount = 1})[0]);

	// Pipeline layout
	{
		vk::PushConstantRange push_constant_range{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .offset = 0,
		        .size = sizeof(FoveationParamsPcs),
		};
		layout = vk.device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &push_constant_range,
		});
	}

	// Pipeline
	{
		auto & spirv = shaders.at("foveate.comp");
		vk::raii::ShaderModule shader(vk.device, {
		                                                 .codeSize = spirv.size() * sizeof(uint32_t),
		                                                 .pCode = spirv.data(),
		                                         });

		float one_over_dim = 1.0 / (RENDER_DISTORTION_IMAGE_DIMENSIONS - 1);

		vk::SpecializationMapEntry specMapEntry{
		        .constantID = 0,
		        .offset = 0,
		        .size = sizeof(float),
		};

		vk::SpecializationInfo specInfo{
		        .mapEntryCount = 1,
		        .pMapEntries = &specMapEntry,
		        .dataSize = sizeof(float),
		        .pData = &one_over_dim,
		};

		pipeline = vk::raii::Pipeline(vk.device, nullptr, vk::ComputePipelineCreateInfo{
		                                                          .stage = {
		                                                                  .stage = vk::ShaderStageFlagBits::eCompute,
		                                                                  .module = *shader,
		                                                                  .pName = "main",
		                                                                  .pSpecializationInfo = &specInfo,
		                                                          },
		                                                          .layout = *layout,
		                                                  });
	}

	std::array<vk::DescriptorSetLayout, 2> layouts = {*ds_layout, *ds_layout};

	auto my_ds = vk.device.allocateDescriptorSets({
	        .descriptorPool = *dp,
	        .descriptorSetCount = 2,
	        .pSetLayouts = layouts.data(),
	});

	ds = {
	        my_ds[0].release(),
	        my_ds[1].release(),
	};
}

void wivrn_foveation_renderer::render_distortion_images(std::array<to_headset::foveation_parameter, 2> foveations, const VkImage * images, const VkImageView * image_views)
{
	cmd_buf.reset();
	cmd_buf.begin(vk::CommandBufferBeginInfo{});

	// images: left position, right position, left derivate, right derivate
	std::array<vk::ImageMemoryBarrier, 4> im_barriers;
	im_barriers.fill({
	        .srcAccessMask = vk::AccessFlagBits::eNone,
	        .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	        .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	        .newLayout = vk::ImageLayout::eGeneral,
	        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                             .baseMipLevel = 0,
	                             .levelCount = 1,
	                             .baseArrayLayer = 0,
	                             .layerCount = 1},
	});
	for (size_t i = 0; i < im_barriers.size(); ++i)
		im_barriers[i].image = vk::Image(images[i]);

	cmd_buf.pipelineBarrier(
	        vk::PipelineStageFlagBits::eTopOfPipe,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        nullptr,
	        nullptr,
	        im_barriers);

	// Monado's distortion shader is patched to read red as position and green as derivate
	for (int eye = 0; eye < 2; ++eye)
	{
		// Descriptor set

		vk::DescriptorImageInfo image_info{
		        .imageView = vk::ImageView(image_views[eye]),
		        .imageLayout = vk::ImageLayout::eGeneral,
		};
		vk.device.updateDescriptorSets(
		        vk::WriteDescriptorSet{
		                .dstSet = ds[eye],
		                .dstBinding = 0,
		                .descriptorCount = 1,
		                .descriptorType = vk::DescriptorType::eStorageImage,
		                .pImageInfo = &image_info,
		        },
		        nullptr);

		auto foveation = foveations[eye];

		FoveationParamsPcs f_params{
		        .a = {foveation.x.a, foveation.y.a},
		        .b = {foveation.x.b, foveation.y.b},
		        .scale = {foveation.x.scale, foveation.y.scale},
		        .center = {foveation.x.center, foveation.y.center},
		};

		cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
		cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *layout, 0, ds[eye], {});
		cmd_buf.pushConstants<FoveationParamsPcs>(*layout, vk::ShaderStageFlagBits::eCompute, 0, f_params);
		cmd_buf.dispatch(dispatch_group_count, dispatch_group_count, 1);
	}

	for (auto & barrier: im_barriers)
	{
		barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eNone;
		barrier.oldLayout = vk::ImageLayout::eGeneral;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	}

	cmd_buf.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eBottomOfPipe,
	        {},
	        nullptr,
	        nullptr,
	        im_barriers);

	cmd_buf.end();
}

xrt_vec2 yaw_pitch(xrt_quat q)
{
	float sine_theta = std::clamp(-2.0f * (q.y * q.z - q.w * q.x), -1.0f, 1.0f);

	float pitch = std::asin(sine_theta);

	if (std::abs(sine_theta) > 0.99999f)
	{
		float scale = std::copysign(2.0, sine_theta);
		return {scale * std::atan2(-q.z, q.w), pitch};
	}

	return {
	        std::atan2(2.0f * (q.x * q.z + q.w * q.y),
	                   q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z),
	        pitch};
}

std::array<xrt_vec2, 2> wivrn_foveation::get_center()
{
	std::lock_guard lock(mutex);

	std::array<xrt_vec2, 2> uvs;
	auto e = yaw_pitch(last_gaze);

	for (int i = 0; i < 2; i++)
	{
		uvs[i].x = (e.x - views[i].fov.angleLeft) / (views[i].fov.angleRight - views[i].fov.angleLeft) * 2 - 1 + center_offset[i].x;
		uvs[i].y = (e.y - views[i].fov.angleDown) / (views[i].fov.angleUp - views[i].fov.angleDown) * 2 - 1 + center_offset[i].y;
	}

	return uvs;
}

void wivrn_foveation::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	std::lock_guard lock(mutex);

	const uint8_t orientation_ok = from_headset::tracking::orientation_valid | from_headset::tracking::orientation_tracked;

	views = tracking.views;

	std::optional<xrt_quat> head;

	for (const auto & pose: tracking.device_poses)
	{
		if (pose.device != device_id::HEAD)
			continue;

		if ((pose.flags & orientation_ok) != orientation_ok)
			return;

		head = xrt_cast(pose.pose.orientation);
		break;
	}

	if (!head.has_value())
		return;

	for (const auto & pose: tracking.device_poses)
	{
		if (pose.device != device_id::EYE_GAZE)
			continue;

		if ((pose.flags & orientation_ok) != orientation_ok)
			return;

		xrt_quat qgaze = xrt_cast(pose.pose.orientation);
		math_quat_unrotate(&qgaze, &head.value(), &last_gaze);
		break;
	}
}

void wivrn_foveation::set_initial_parameters(std::array<to_headset::foveation_parameter, 2> p)
{
	std::lock_guard lock(mutex);

	center_offset[0] = {p[0].x.center, p[0].y.center};
	center_offset[1] = {p[1].x.center, p[1].y.center};
}
} // namespace wivrn
