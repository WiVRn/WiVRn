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

#include "video_encoder_pyrowave.h"
#include "encoder/encoder_settings.h"
#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"

#include <ranges>

namespace wivrn
{
video_encoder_pyrowave::video_encoder_pyrowave(wivrn_vk_bundle & vk, encoder_settings & settings, float fps, uint8_t stream_idx) :
        video_encoder(stream_idx, settings.channels, 50, true),
        enc(vk.physical_device, vk.device, settings.width, settings.height, PyroWave::ChromaSubsampling::Chroma420),
        encoded_size(settings.bitrate / fps)
{
	size_t meta_size = enc.get_meta_required_size();
	meta_buf = buffer_allocation(
	        vk.device,
	        {
	                .size = meta_size,
	                .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
	        },
	        {
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        "pyrowave encoder meta buffer");
	data_buf = buffer_allocation(
	        vk.device,
	        {
	                .size = encoded_size + 2 * meta_size,
	                .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
	        },
	        {
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        "pyrowave encoder data buffer");

	if (not(meta_buf.properties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
	{
		meta_buf_staging = buffer_allocation(
		        vk.device,
		        {
		                .size = meta_buf.info().size,
		                .usage = vk::BufferUsageFlagBits::eTransferDst,
		        },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "pyrowave encoder meta staging buffer");
	}
	if (not(data_buf.properties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
	{
		data_buf_staging = buffer_allocation(
		        vk.device,
		        {
		                .size = data_buf.info().size,
		                .usage = vk::BufferUsageFlagBits::eTransferDst,
		        },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "pyrowave encoder data staging buffer");
	}
}

std::pair<bool, vk::Semaphore> video_encoder_pyrowave::present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t frame_index)
{
	std::array<vk::ImageView, 3> image_view;

	if (auto it = image_views.find(y_cbcr); it != image_views.end())
	{
		std::ranges::copy(it->second, image_view.begin());
	}
	else
	{
		auto y = enc.device.createImageView(
		        vk::ImageViewCreateInfo{
		                .image = y_cbcr,
		                .viewType = vk::ImageViewType::e2D,
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
		                        .levelCount = 1,
		                        .baseArrayLayer = uint32_t(channels),
		                        .layerCount = 1,
		                }});
		auto cb = enc.device.createImageView(
		        vk::ImageViewCreateInfo{
		                .image = y_cbcr,
		                .viewType = vk::ImageViewType::e2D,
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::ePlane1,
		                        .levelCount = 1,
		                        .baseArrayLayer = uint32_t(channels),
		                        .layerCount = 1,
		                }});
		auto cr = enc.device.createImageView(
		        vk::ImageViewCreateInfo{
		                .image = y_cbcr,
		                .viewType = vk::ImageViewType::e2D,
		                .components = {
		                        .r = vk::ComponentSwizzle::eG,
		                },
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::ePlane1,
		                        .levelCount = 1,
		                        .baseArrayLayer = uint32_t(channels),
		                        .layerCount = 1,
		                }});

		image_view = {y, cb, cr};
		image_views.emplace(y_cbcr, std::array{std::move(y), std::move(cb), std::move(cr)});
	}
	PyroWave::Encoder::BitstreamBuffers buffers{
	        .meta = {
	                .buffer = meta_buf,
	                .size = meta_buf.info().size,
	        },
	        .bitstream = {
	                .buffer = data_buf,
	                .size = data_buf.info().size,
	        },
	        .target_size = encoded_size,
	};
	if (not enc.encode(cmd_buf, image_view, buffers))
		U_LOG_W("pyrowave encode failed");

	if (meta_buf_staging)
		cmd_buf.copyBuffer(meta_buf, meta_buf_staging, vk::BufferCopy{.size = buffers.meta.size});
	if (data_buf_staging)
		cmd_buf.copyBuffer(data_buf, data_buf_staging, vk::BufferCopy{.size = buffers.bitstream.size});
	return {false, nullptr};
}

std::optional<video_encoder::data> video_encoder_pyrowave::encode(bool idr, std::chrono::steady_clock::time_point pts, uint8_t slot)
{
	reordered_packet_buffer.resize(8 * 1024 * 1024);
	packets.resize(enc.compute_num_packets(meta_buf_staging ? meta_buf_staging.map() : meta_buf.map(), 8 * 1024));
	enc.packetize(
	        packets.data(),
	        8 * 1024,
	        reordered_packet_buffer.data(),
	        reordered_packet_buffer.size(),
	        meta_buf_staging ? meta_buf_staging.map() : meta_buf.map(),
	        data_buf_staging ? data_buf_staging.map() : data_buf.map());
	for (auto & p: packets)
		SendData({reordered_packet_buffer.data() + p.offset, p.size}, &p == &packets.back());

	return {};
}

video_encoder_pyrowave::~video_encoder_pyrowave() {}
} // namespace wivrn
