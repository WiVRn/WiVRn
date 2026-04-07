/*
 * WiVRn VR streaming
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

#include "video_encoder_raw.h"

#include "encoder/encoder_settings.h"
#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"

namespace
{
class dummy_idr_handler : public wivrn::idr_handler
{
public:
	void on_feedback(const wivrn::from_headset::feedback &) override {};
	void reset() override {};
	bool should_skip(uint64_t frame_id) override
	{
		return false;
	};
};
vk::raii::CommandPool make_cmd_pool(wivrn::vk_bundle & vk, uint8_t stream_idx)
{
	auto res = vk.device.createCommandPool(vk::CommandPoolCreateInfo{

	        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
	        .queueFamilyIndex = *vk.transfer_queue ? vk.transfer_queue_family_index : vk.queue_family_index,
	});
	vk.name(res, std::format("raw encoder {} command pool", stream_idx));
	return res;
}
} // namespace

wivrn::video_encoder_raw::video_encoder_raw(
        wivrn::vk_bundle & vk,
        const encoder_settings & settings,
        uint8_t stream_idx) :
        video_encoder(vk,
                      stream_idx,
                      *vk.transfer_queue ? vk.transfer_queue_family_index : vk.queue_family_index,
                      settings,
                      std::make_unique<dummy_idr_handler>(),
                      true),
        vk{vk},
        cmd_pool{make_cmd_pool(vk, stream_idx)}
{
	if (settings.bit_depth != 8)
		throw std::runtime_error("Raw encoding is only supported for 8 bit");

	vk::DeviceSize buffer_size = extent.width * extent.height;
	if (stream_idx < 2)
		buffer_size += buffer_size / 2;

	auto command_buffers = vk.device.allocateCommandBuffers(
	        {.commandPool = *cmd_pool,
	         .commandBufferCount = num_slots});

	for (size_t i = 0; i < num_slots; ++i)
	{
		in[i].cmd = std::move(command_buffers[i]);
		in[i].buffer = buffer_allocation(
		        vk.device,
		        {
		                .size = buffer_size,
		                .usage = vk::BufferUsageFlagBits::eTransferDst,
		        },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		        },
		        "raw stream buffer");
		in[i].fence = vk::raii::Fence(vk.device,
		                              vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
	}
}

void wivrn::video_encoder_raw::present_image(vk::Image y_cbcr, vk::SemaphoreSubmitInfo compositor_sem, uint8_t slot, uint64_t)
{
	if (vk.device.waitForFences(*in[slot].fence, true, 1'000'000'000) == vk::Result::eTimeout)
	{
		U_LOG_E("Timeout on stream %d", stream_idx);
		return;
	}

	auto & cmd = in[slot].cmd;
	cmd.reset();
	cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	if (target_queue != vk.queue_family_index)
	{
		vk::ImageMemoryBarrier2 barrier{
		        .dstStageMask = vk::PipelineStageFlagBits2KHR::eTransfer,
		        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
		        .srcQueueFamilyIndex = vk.queue_family_index,
		        .dstQueueFamilyIndex = target_queue,
		        .image = y_cbcr,
		        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .baseArrayLayer = stream_idx,
		                             .layerCount = 1},
		};
		cmd.pipelineBarrier2({
		        .imageMemoryBarrierCount = 1,
		        .pImageMemoryBarriers = &barrier,
		});
	}

	std::array regions{
	        vk::BufferImageCopy{
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
	                        .baseArrayLayer = stream_idx,
	                        .layerCount = 1,
	                },
	                .imageExtent = {
	                        .width = extent.width,
	                        .height = extent.height,
	                        .depth = 1,
	                },
	        },
	        vk::BufferImageCopy{
	                .bufferOffset = extent.width * extent.height,
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane1,
	                        .baseArrayLayer = stream_idx,
	                        .layerCount = 1,
	                },
	                .imageExtent = {
	                        .width = extent.width / 2,
	                        .height = extent.height / 2,
	                        .depth = 1,
	                },
	        },
	};
	cmd.copyImageToBuffer(
	        y_cbcr,
	        vk::ImageLayout::eGeneral,
	        in[slot].buffer,
	        std::span(regions.data(),
	                  stream_idx < 2 ? 2 : 1));

	cmd.end();

	std::unique_lock lock(*vk.transfer_queue ? vk.transfer_queue_mutex : vk.queue_mutex);
	vk::CommandBufferSubmitInfo cmd_info{
	        .commandBuffer = *cmd,
	};
	compositor_sem.stageMask = vk::PipelineStageFlagBits2::eTransfer;

	vk.device.resetFences(*in[slot].fence);
	(*vk.transfer_queue ? vk.transfer_queue : vk.queue)
	        .submit2(vk::SubmitInfo2{
	                         .waitSemaphoreInfoCount = 1,
	                         .pWaitSemaphoreInfos = &compositor_sem,
	                         .commandBufferInfoCount = 1,
	                         .pCommandBufferInfos = &cmd_info,
	                 },
	                 *in[slot].fence);
}

std::optional<wivrn::video_encoder::data> wivrn::video_encoder_raw::encode(uint8_t slot, uint64_t frame_id)
{
	if (vk.device.waitForFences(*in[slot].fence, true, 1'000'000'000) == vk::Result::eTimeout)
	{
		U_LOG_W("Timeout on stream %d", stream_idx);
		return {};
	}

	return wivrn::video_encoder::data{
	        .encoder = this,
	        .span = std::span<uint8_t>((uint8_t *)in[slot].buffer.map(), in[slot].buffer.info().size),
	};
}
