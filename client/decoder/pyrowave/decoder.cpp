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

#include "decoder.h"

#include "application.h"
#include "scenes/stream.h"

namespace wivrn
{
decoder::decoder(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        uint32_t vk_queue_family_index,
        const wivrn::to_headset::video_stream_description::item & description,
        float fps,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> scene,
        shard_accumulator * accumulator) :
        ycbcr_conversion(device, {
                                         .format = vk::Format::eG8B8R83Plane420Unorm,
                                         .ycbcrModel = vk::SamplerYcbcrModelConversion(description.color_model.value_or(VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709)),
                                         .ycbcrRange = vk::SamplerYcbcrRange(description.range.value_or(VK_SAMPLER_YCBCR_RANGE_ITU_FULL)),
                                         .chromaFilter = vk::Filter::eNearest,
                                 }),
        sampler_(device, vk::StructureChain{
                                 vk::SamplerCreateInfo{
                                         .magFilter = vk::Filter::eNearest,
                                         .minFilter = vk::Filter::eNearest,
                                         .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                         .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                         .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                         .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                         .maxAnisotropy = 1,
                                 },
                                 vk::SamplerYcbcrConversionInfo{
                                         .conversion = *ycbcr_conversion,
                                 },
                         }
                                 .get()),
        description(description),
        weak_scene(scene),
        accumulator(accumulator),
        command_pool(device, vk::CommandPoolCreateInfo{
                                     .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                     .queueFamilyIndex = vk_queue_family_index,
                             }),
        cmd_buf(std::move(device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                .commandPool = *command_pool,
                .commandBufferCount = 1,
        })[0])),
        fence(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled}),
        dec(physical_device, device, description.width, description.height, PyroWave::ChromaSubsampling::Chroma420)
{
	std::array formats = {
	        vk::Format::eR8Unorm,
	        vk::Format::eG8B8R83Plane420Unorm,
	};
	vk::ImageFormatListCreateInfo formats_info{
	        .viewFormatCount = formats.size(),
	        .pViewFormats = formats.data(),
	};
	size_t i = 0;
	for (auto & item: image_pool)
	{
		item.image = image_allocation(
		        device,
		        vk::ImageCreateInfo{
		                .pNext = &formats_info,
		                .flags = vk::ImageCreateFlagBits::eExtendedUsage | vk::ImageCreateFlagBits::eMutableFormat,
		                .imageType = vk::ImageType::e2D,
		                .format = formats.back(),
		                .extent = {
		                        .width = description.width,
		                        .height = description.height,
		                        .depth = 1,
		                },
		                .mipLevels = 1,
		                .arrayLayers = 1,
		                .tiling = vk::ImageTiling::eOptimal,
		                .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage,
		        },
		        {
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        std::format("pyrowave decoder {} image {}", stream_index, i));

		vk::SamplerYcbcrConversionInfo conv{
		        .conversion = *ycbcr_conversion,
		};
		vk::ImageViewUsageCreateInfo usage{
		        .pNext = &conv,
		        .usage = vk::ImageUsageFlagBits::eSampled,
		};
		vk::ImageViewCreateInfo view_info{
		        .pNext = &usage,
		        .image = item.image,
		        .viewType = vk::ImageViewType::e2D,
		        .format = formats.back(),
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .levelCount = 1,
		                .layerCount = 1,
		        },
		};
		item.view_full = vk::raii::ImageView(device, view_info);

		usage.pNext = nullptr;
		usage.usage |= vk::ImageUsageFlagBits::eStorage;
		view_info.format = formats[0];
		view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::ePlane0;
		item.view_y = vk::raii::ImageView(device, view_info);
		view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::ePlane1;
		item.view_cb = vk::raii::ImageView(device, view_info);
		view_info.subresourceRange.aspectMask = vk::ImageAspectFlagBits::ePlane2;

		item.view_cr = vk::raii::ImageView(device, view_info);
		++i;
	}

	worker = std::thread([&] { worker_function(); });
}

decoder::~decoder()
{
	exiting = true;
	pending.lock().notify_all();
	if (worker.joinable())
		worker.join();
}

void decoder::push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial)
{
	if (data.size() == 1)
	{
		dec.push_packet(data[0].data(), data[0].size());
		return;
	}
	// FIXME: accept sparse data in push_packet
	std::vector<uint8_t> flat;
	for (const auto & i: data)
		flat.insert(flat.end(), i.begin(), i.end());
	dec.push_packet(flat.data(), flat.size());
}

void decoder::frame_completed(
        const from_headset::feedback & feedback,
        const to_headset::video_stream_data_shard::view_info_t & view_info)
{
	auto item = get_free();
	if (not item)
	{
		spdlog::warn("No image available in pool, discard frame");
		dec.clear();
		return;
	}
	std::array views{*item->view_y, *item->view_cb, *item->view_cr};
	auto res = dec.device.waitForFences(*fence, true, UINT64_MAX);
	if (res != vk::Result::eSuccess)
		spdlog::warn("waitForFences failed");
	auto locked = pending.lock();
	dec.device.resetFences(*fence);
	cmd_buf.reset();
	cmd_buf.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	if (item->current_layout != vk::ImageLayout::eGeneral)
	{
		item->current_layout = vk::ImageLayout::eGeneral;
		vk::ImageMemoryBarrier barrier{
		        .srcAccessMask = vk::AccessFlagBits::eNone,
		        .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
		        .oldLayout = vk::ImageLayout::eUndefined,
		        .newLayout = vk::ImageLayout::eGeneral,
		        .image = item->image,
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .levelCount = 1,
		                .layerCount = 1,
		        },
		};
		cmd_buf.pipelineBarrier(
		        vk::PipelineStageFlagBits::eAllCommands,
		        vk::PipelineStageFlagBits::eTransfer,
		        {},
		        {},
		        {},
		        barrier);
	}
	dec.decode(cmd_buf, views);
	cmd_buf.end();

	application::get_queue().lock()->submit(
	        vk::SubmitInfo{
	                .commandBufferCount = 1,
	                .pCommandBuffers = &*cmd_buf,
	        },
	        *fence);

	*locked = {
	        .feedback = feedback,
	        .view_info = view_info,
	        .img = item,
	};
	locked.notify_all();
}

decoder::image * decoder::get_free()
{
	for (auto & item: image_pool)
	{
		if (item.free.exchange(false))
			return &item;
	}
	return nullptr;
}

void decoder::worker_function()
{
	while (not exiting)
	{
		auto locked = pending.lock();
		locked.wait([&]() { return exiting or locked->img != nullptr; });
		if (exiting)
			return;

		try
		{
			auto res = dec.device.waitForFences(*fence, true, UINT64_MAX);
			if (res != vk::Result::eSuccess)
				spdlog::warn("waitForFences failed");

			auto scene = weak_scene.lock();
			scene->push_blit_handle(
			        accumulator,
			        std::make_shared<blit_handle>(
			                locked->feedback,
			                locked->view_info,
			                locked->img->view_full,
			                locked->img->image,
			                &locked->img->current_layout,
			                locked->img->free));
		}
		catch (...)
		{}
		locked->img = nullptr;
	}
}

std::vector<wivrn::video_codec> decoder::supported_codecs()
{
	return {video_codec::pyrowave};
}

decoder::blit_handle::~blit_handle()
{
	free = true;
}
} // namespace wivrn
