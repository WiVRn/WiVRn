/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "image_writer.h"

#include "utils/thread_safe.h"
#include <cassert>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void write_image(
        vk::raii::Device & device,
        thread_safe<vk::raii::Queue> & queue,
        uint32_t queue_family_index,
        const std::filesystem::path & path,
        vk::Image image,
        const vk::ImageCreateInfo & info)
{
	assert(info.usage & vk::ImageUsageFlagBits::eTransferSrc);
	assert(info.extent.depth == 1);
	assert(info.format == vk::Format::eR8G8B8A8Srgb);

	buffer_allocation output_buffer{
	        device,
	        vk::BufferCreateInfo{
	                .size = info.extent.height * info.extent.width * 4,
	                .usage = vk::BufferUsageFlagBits::eTransferDst,
	        },
	        VmaAllocationCreateInfo{
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
	                .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO,
	        },
	        "Saved image buffer"};

	vk::raii::CommandPool cp{
	        device,
	        vk::CommandPoolCreateInfo{
	                .queueFamilyIndex = queue_family_index,
	        }};

	vk::raii::CommandBuffer command_buffer = std::move(device.allocateCommandBuffers(
	        {
	                .commandPool = *cp,
	                .level = vk::CommandBufferLevel::ePrimary,
	                .commandBufferCount = 1,
	        })[0]);

	vk::raii::Fence fence = device.createFence({});

	command_buffer.begin(vk::CommandBufferBeginInfo{});

	command_buffer.pipelineBarrier(
	        vk::PipelineStageFlagBits::eColorAttachmentOutput,
	        vk::PipelineStageFlagBits::eTransfer,
	        vk::DependencyFlags{},
	        {},
	        {},
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
	                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
	                .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
	                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
	                .image = image,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .baseMipLevel = 0,
	                        .levelCount = 1,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	        });

	command_buffer.copyImageToBuffer(
	        image,
	        vk::ImageLayout::eTransferSrcOptimal,
	        output_buffer,
	        vk::BufferImageCopy{
	                .bufferOffset = 0,
	                .bufferRowLength = 0,
	                .bufferImageHeight = 0,
	                .imageSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .mipLevel = 0,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	                .imageOffset = {0, 0, 0},
	                .imageExtent = info.extent,
	        });

	command_buffer.end();
	queue.lock()->submit(vk::SubmitInfo{
	                             .commandBufferCount = 1,
	                             .pCommandBuffers = &*command_buffer,
	                     },
	                     *fence);

	if (auto result = device.waitForFences(*fence, true, 1'000'000'000); result == vk::Result::eSuccess)
		stbi_write_png(path.c_str(), info.extent.width, info.extent.height, 4, output_buffer.data(), 0);
}
