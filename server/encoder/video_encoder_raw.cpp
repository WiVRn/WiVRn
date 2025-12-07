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
} // namespace

wivrn::video_encoder_raw::video_encoder_raw(
        wivrn_vk_bundle & vk,
        const encoder_settings & settings,
        uint8_t stream_idx) :
        video_encoder(stream_idx, settings, std::make_unique<dummy_idr_handler>(), true)
{
	if (settings.bit_depth != 8)
		throw std::runtime_error("Raw encoding is only supported for 8 bit");

	vk::DeviceSize buffer_size = extent.width * extent.height;
	if (stream_idx < 2)
		buffer_size += buffer_size / 2;
	for (auto & slot: buffers)
	{
		slot = buffer_allocation(
		        vk.device,
		        {
		                .size = buffer_size,
		                .usage = vk::BufferUsageFlagBits::eTransferDst,
		        },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "raw stream buffer");
	}
}

std::pair<bool, vk::Semaphore> wivrn::video_encoder_raw::present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t)
{
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
	cmd_buf.copyImageToBuffer(
	        y_cbcr,
	        vk::ImageLayout::eTransferSrcOptimal,
	        buffers[slot],
	        std::span(regions.data(),
	                  stream_idx == 2 ? 2 : 1));
	return {false, nullptr};
}

std::optional<wivrn::video_encoder::data> wivrn::video_encoder_raw::encode(uint8_t slot, uint64_t frame_id)
{
	return wivrn::video_encoder::data{
	        .encoder = this,
	        .span = std::span<uint8_t>((uint8_t *)buffers[slot].map(), buffers[slot].info().size),
	};
}
