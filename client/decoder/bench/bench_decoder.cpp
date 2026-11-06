/*
 * WiVRn VR streaming
 * Copyright (C) 2026  WiVRn Contributors
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

#include "bench_decoder.h"

#include "application.h"
#include "scenes/stream.h"

namespace
{
struct bench_blit_handle : public wivrn::decoder::blit_handle
{
	bench_blit_handle(
	        const wivrn::from_headset::feedback & feedback,
	        const wivrn::to_headset::video_stream_data_shard::view_info_t & view_info,
	        vk::ImageView image_view,
	        vk::Image image,
	        vk::Extent2D extent,
	        vk::ImageLayout & current_layout,
	        vk::Semaphore semaphore,
	        uint64_t & semaphore_val) :
	        wivrn::decoder::blit_handle{feedback, view_info, image_view, image, extent, current_layout, semaphore, &semaphore_val} {}
};

} // namespace

namespace wivrn::bench
{
decoder::decoder(
        vk::raii::Device & device,
        vk::raii::PhysicalDevice & physical_device,
        uint32_t vk_queue_family_index,
        const wivrn::to_headset::video_stream_description & description,
        uint8_t stream_index,
        std::weak_ptr<scenes::stream> scene,
        shard_accumulator * accumulator) :
        device(device),
        sampler_(device, vk::SamplerCreateInfo{
                                 .magFilter = vk::Filter::eLinear,
                                 .minFilter = vk::Filter::eLinear,
                                 .mipmapMode = vk::SamplerMipmapMode::eNearest,
                                 .addressModeU = vk::SamplerAddressMode::eClampToEdge,
                                 .addressModeV = vk::SamplerAddressMode::eClampToEdge,
                                 .addressModeW = vk::SamplerAddressMode::eClampToEdge,
                                 .unnormalizedCoordinates = false,
                         }),
        semaphore(device, vk::StructureChain{
                                  vk::SemaphoreCreateInfo{},
                                  vk::SemaphoreTypeCreateInfo{
                                          .semaphoreType = vk::SemaphoreType::eTimeline,
                                          .initialValue = 0,
                                  },
                          }
                                  .get()),
        weak_scene(scene),
        accumulator(accumulator)
{
	// Same format as the ffmpeg backend's decoded_images (ffmpeg_decoder.cpp): a plain sRGB
	// image with a plain sampler, no YCbCr conversion, so this takes the same pipeline/
	// descriptor-layout path as the normally-used decoder.
	vk::Extent2D extent{
	        .width = description.width,
	        .height = description.height / (stream_index == 2 ? 2u : 1u),
	};
	vk::Format format = vk::Format::eA8B8G8R8SrgbPack32;

	image = image_allocation(
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
	        "bench decoder image");

	view = vk::raii::ImageView(
	        device,
	        vk::ImageViewCreateInfo{
	                .image = image,
	                .viewType = vk::ImageViewType::e2D,
	                .format = format,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .levelCount = 1,
	                        .layerCount = 1,
	                },
	        });

	// One-time setup: there is nothing to decode, so the image content never changes after
	// this; every frame just re-signals the semaphore against the same already-ready image.
	vk::raii::CommandPool command_pool(device, vk::CommandPoolCreateInfo{
	                                                   .queueFamilyIndex = vk_queue_family_index,
	                                           });
	vk::raii::CommandBuffers cmds(device, vk::CommandBufferAllocateInfo{
	                                              .commandPool = *command_pool,
	                                              .commandBufferCount = 1,
	                                      });
	vk::raii::Fence fence(device, vk::FenceCreateInfo{});

	vk::CommandBuffer cmd = *cmds[0];
	cmd.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
	vk::ImageMemoryBarrier to_dst{
	        .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	        .oldLayout = vk::ImageLayout::eUndefined,
	        .newLayout = vk::ImageLayout::eTransferDstOptimal,
	        .image = image,
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .levelCount = 1,
	                .layerCount = 1,
	        },
	};
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, to_dst);
	cmd.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue{std::array{0.5f, 0.5f, 0.5f, 1.0f}}, {vk::ImageSubresourceRange{
	                                                                                                                                  .aspectMask = vk::ImageAspectFlagBits::eColor,
	                                                                                                                                  .levelCount = 1,
	                                                                                                                                  .layerCount = 1,
	                                                                                                                          }});
	vk::ImageMemoryBarrier to_read{
	        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
	        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
	        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
	        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	        .image = image,
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .levelCount = 1,
	                .layerCount = 1,
	        },
	};
	cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, to_read);
	cmd.end();

	application::get_queue().lock()->submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &cmd}, *fence);
	if (device.waitForFences(*fence, true, UINT64_MAX) != vk::Result::eSuccess)
		spdlog::warn("bench decoder: waitForFences failed");

	current_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

void decoder::push_data(std::span<std::span<const uint8_t>>, uint64_t, bool)
{
	// Nothing to decode: discard the bitstream, don't even copy it.
}

void decoder::frame_completed(
        const from_headset::feedback & feedback,
        const to_headset::video_stream_data_shard::view_info_t & view_info)
{
	auto handle = std::make_shared<bench_blit_handle>(
	        feedback,
	        view_info,
	        *view,
	        image,
	        vk::Extent2D{image.info().extent.width, image.info().extent.height},
	        current_layout,
	        *semaphore,
	        semaphore_val);

	device.signalSemaphore(vk::SemaphoreSignalInfo{
	        .semaphore = *semaphore,
	        .value = ++semaphore_val,
	});

	if (auto scene = weak_scene.lock())
		scene->push_blit_handle(accumulator, std::move(handle));
}

void decoder::supported_codecs(std::vector<wivrn::video_codec> & res)
{
	res.push_back(wivrn::video_codec::h264);
	res.push_back(wivrn::video_codec::h265);
	res.push_back(wivrn::video_codec::av1);
}
} // namespace wivrn::bench
