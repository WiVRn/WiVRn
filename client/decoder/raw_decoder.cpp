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

#include "raw_decoder.h"

#include "application.h"
#include "scenes/stream.h"

namespace
{
struct raw_blit_handle : public wivrn::decoder::blit_handle
{
	std::atomic_bool & free;

	raw_blit_handle(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info,
	        vk::ImageView image_view,
	        vk::Image image,
	        vk::Extent2D extent,
	        vk::ImageLayout & current_layout,
	        vk::Semaphore semaphore,
	        uint64_t & semaphore_val,
	        std::atomic_bool & free) :
	        wivrn::decoder::blit_handle{feedback, view_info, image_view, image, extent, current_layout, semaphore, &semaphore_val},
	        free(free) {}
	~raw_blit_handle()
	{
		free = true;
	};
};

vk::raii::Sampler make_sampler(
        vk::raii::Device & device,
        vk::SamplerYcbcrConversion ycbcr_conversion)
{
	vk::StructureChain info{
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
	                .conversion = ycbcr_conversion,
	        },
	};
	if (ycbcr_conversion == VK_NULL_HANDLE)
		info.unlink<vk::SamplerYcbcrConversionInfo>();
	return vk::raii::Sampler(device, info.get());
}
} // namespace

namespace wivrn
{
raw_decoder::raw_decoder(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        uint32_t vk_queue_family_index,
        const wivrn::to_headset::video_stream_description & description,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> scene,
        shard_accumulator * accumulator) :
        device(device),
        ycbcr_conversion(stream_index == 2 ? nullptr : vk::raii::SamplerYcbcrConversion(device, {
                                                                                                        .format = vk::Format::eG8B8R82Plane420Unorm,
                                                                                                        .ycbcrModel = vk::SamplerYcbcrModelConversion::eYcbcr709,
                                                                                                        .ycbcrRange = vk::SamplerYcbcrRange::eItuFull,
                                                                                                        .chromaFilter = vk::Filter::eNearest,
                                                                                                })),
        sampler_(make_sampler(device, *ycbcr_conversion)),
        command_pool(device, vk::CommandPoolCreateInfo{
                                     .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                                     .queueFamilyIndex = vk_queue_family_index,
                             }),
        cmd(device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{
                .commandPool = *command_pool,
                .commandBufferCount = 1,
        })[0]
                    .release()),
        fence(device, vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled}),
        stream_index(stream_index),
        extent{
                .width = description.width,
                .height = description.height / (stream_index == 2 ? 2u : 1u),
        },
        weak_scene(scene),
        accumulator(accumulator)
{
	vk::DeviceSize buffer_size = extent.width * extent.height;
	vk::Format format{};
	switch (stream_index)
	{
		case 0: // colour left
		case 1: // colour right
			buffer_size += buffer_size / 2;
			format = vk::Format::eG8B8R82Plane420Unorm;
			break;
		case 2: // alpha (monochrome)
			format = vk::Format::eR8Unorm;
			break;
	}
	for (auto & i: input)
	{
		i = buffer_allocation(
		        device,
		        {
		                .size = buffer_size,
		                .usage = vk::BufferUsageFlagBits::eTransferSrc,
		        },
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "raw stream buffer");
	}
	input_pos = (uint8_t *)input[0].map();

	for (auto & item: image_pool)
	{
		item.image = image_allocation(
		        device,
		        vk::ImageCreateInfo{
		                .imageType = vk::ImageType::e2D,
		                .format = format,
		                .extent = {
		                        .width = extent.width,
		                        .height = extent.height,
		                        .depth = 1,
		                },
		                .mipLevels = 1,
		                .arrayLayers = 1,
		                .tiling = vk::ImageTiling::eOptimal,
		                .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
		        },
		        {
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "raw image");

		vk::SamplerYcbcrConversionInfo conv{
		        .conversion = *ycbcr_conversion,
		};
		vk::ImageViewCreateInfo view_info{
		        .pNext = format == vk::Format::eG8B8R82Plane420Unorm ? &conv : nullptr,
		        .image = item.image,
		        .viewType = vk::ImageViewType::e2D,
		        .format = format,
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .levelCount = 1,
		                .layerCount = 1,
		        },
		};
		item.view_full = vk::raii::ImageView(device, view_info);

		item.semaphore = vk::raii::Semaphore(
		        device,
		        vk::StructureChain{
		                vk::SemaphoreCreateInfo{},
		                vk::SemaphoreTypeCreateInfo{
		                        .semaphoreType = vk::SemaphoreType::eTimeline,
		                },
		        }
		                .get());
	}
}

void raw_decoder::push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial)
{
	if (frame_index != current_frame)
	{
		input_pos = (uint8_t *)input[0].map();
		current_frame = frame_index;
	}
	for (const auto & item: data)
	{
		memcpy(input_pos, item.data(), item.size_bytes());
		input_pos += item.size();
	}
}

void raw_decoder::frame_completed(
        const from_headset::feedback & feedback,
        const to_headset::video_stream_data_shard::view_info_t & view_info)
{
	auto item = get_free();
	if (not item)
	{
		spdlog::warn("No image available in pool, discard frame");
		return;
	}

	auto handle = std::make_shared<raw_blit_handle>(
	        feedback,
	        view_info,
	        *item->view_full,
	        item->image,
	        extent,
	        item->current_layout,
	        *item->semaphore,
	        item->semaphore_val,
	        item->free);

	auto res = device.waitForFences(*fence, true, UINT64_MAX);
	if (res != vk::Result::eSuccess)
		spdlog::warn("waitForFences failed");

	cmd.reset();
	cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	if (item->current_layout != vk::ImageLayout::eTransferDstOptimal)
	{
		item->current_layout = vk::ImageLayout::eTransferDstOptimal;
		vk::ImageMemoryBarrier barrier{
		        .srcAccessMask = vk::AccessFlagBits::eNone,
		        .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
		        .oldLayout = vk::ImageLayout::eUndefined,
		        .newLayout = item->current_layout,
		        .image = item->image,
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .levelCount = 1,
		                .layerCount = 1,
		        },
		};
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eAllCommands,
		        vk::PipelineStageFlagBits::eTransfer,
		        {},
		        {},
		        {},
		        barrier);
	}

	std::array regions{
	        vk::BufferImageCopy{
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
	                        .layerCount = 1,
	                },
	                .imageExtent = {
	                        .width = extent.width,
	                        .height = extent.height,
	                        .depth = 1,
	                },
	        },
	        vk::BufferImageCopy{
	                .bufferOffset = vk::DeviceSize(extent.width * extent.height),
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane1,
	                        .layerCount = 1,
	                },
	                .imageExtent = {
	                        .width = extent.width / 2u,
	                        .height = extent.height / 2u,
	                        .depth = 1,
	                },
	        },
	};

	cmd.copyBufferToImage(
	        input[0],
	        item->image,
	        item->current_layout,
	        stream_index ? 2 : 1,
	        regions.data());

	vk::ImageMemoryBarrier barrier{
	        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
	        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
	        .oldLayout = item->current_layout,
	        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	        .image = item->image,
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .levelCount = 1,
	                .layerCount = 1,
	        },
	};
	item->current_layout = barrier.newLayout;
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eTransfer,
	        vk::PipelineStageFlagBits::eFragmentShader,
	        {},
	        {},
	        {},
	        barrier);

	cmd.end();

	device.resetFences(*fence);
	application::get_queue().lock()->submit(
	        vk::StructureChain{
	                vk::SubmitInfo{
	                        .commandBufferCount = 1,
	                        .pCommandBuffers = &cmd,
	                        .signalSemaphoreCount = 1,
	                        .pSignalSemaphores = &*item->semaphore,
	                },
	                vk::TimelineSemaphoreSubmitInfo{
	                        .signalSemaphoreValueCount = 1,
	                        .pSignalSemaphoreValues = &++item->semaphore_val,
	                },
	        }
	                .get(),
	        *fence);

	if (auto scene = weak_scene.lock())
		scene->push_blit_handle(accumulator, std::move(handle));

	std::swap(input[0], input[1]);
	input_pos = (uint8_t *)input[0].map();
}

raw_decoder::image * raw_decoder::get_free()
{
	for (auto & item: image_pool)
	{
		if (item.free.exchange(false))
			return &item;
	}
	return nullptr;
}

std::vector<wivrn::video_codec> raw_decoder::supported_codecs()
{
	return {video_codec::raw};
}
} // namespace wivrn
