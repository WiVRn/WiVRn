/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "yuv_converter.h"

#include <map>
#include <vector>

extern const std::map<std::string, std::vector<uint32_t>> shaders;

// clang-format off
#define SHADER_BGRA 1
#if SHADER_BGRA
const float COLORSPACE_BT709[3][4] = {
//         B        G        R      A
/* Y */{ 0.0722,  0.7152,  0.2126, 0.0},
/* Cb*/{ 0.5   , -0.3854, -0.1146, 0.0},
/* Cr*/{-0.0458, -0.4542,  0.5   , 0.0},
//clang-format: on
};
#else
const float COLORSPACE_BT709[3][4] = {
//         R         G        B      A
/* Y */ { 0.2126,  0.7152,  0.0722, 0.0},
/* Cb*/ {-0.1146, -0.3854,  0.5   , 0.0},
/* Cr*/ { 0.5   , -0.4542, -0.0458, 0.0},
};
#endif

static vk::Format view_format(vk::Format image_format)
{
	switch (image_format)
	{
		case vk::Format::eB8G8R8A8Srgb:
		case vk::Format::eB8G8R8A8Unorm:
			// Intel anv appears to get the order wrong
			// swizzle components in the colorspace
			return vk::Format::eR8G8B8A8Unorm;
		default:
			throw std::runtime_error("YUV conversion not implemented for " + vk::to_string(image_format));
	};
}

yuv_converter::yuv_converter() {}
yuv_converter::yuv_converter(vk::PhysicalDevice physical_device, vk::raii::Device & device, vk::Image rgb, vk::Format fmt, vk::Extent2D extent) :
        extent(extent), rgb(rgb)
{
	auto view_fmt = view_format(fmt);

	struct plane_t
	{
		vk::Format format;
		vk::Extent3D extent;
		image_allocation & image;
		vk::raii::ImageView & view;
	};

	// Description of planes for 4:2:0 subsampling
	std::array planes = {
	        plane_t{
	                .format = vk::Format::eR8Unorm,
	                .extent = {extent.width, extent.height, 1},
	                .image = luma,
	                .view = view_luma,
	        },
	        plane_t{
	                .format = vk::Format::eR8G8Unorm,
	                .extent = {extent.width / 2, extent.height / 2, 1},
	                .image = chroma,
	                .view = view_chroma,
	        },
	};

	// Input image view
	{
		vk::ImageViewUsageCreateInfo usage{
		        .usage = vk::ImageUsageFlagBits::eStorage,
		};
		view_rgb = device.createImageView(
		        {
		                .pNext = &usage,
		                .image = rgb,
		                .viewType = vk::ImageViewType::e2D,
		                .format = view_fmt,
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::eColor,
		                        .levelCount = 1,
		                        .layerCount = 1,
		                },
		        });
	}

	// Output images
	for (auto & plane: planes)
	{
		plane.image = image_allocation(
				device,
		        {
		                .imageType = vk::ImageType::e2D,
		                .format = plane.format,
		                .extent = plane.extent,
		                .mipLevels = 1,
		                .arrayLayers = 1,
		                .samples = vk::SampleCountFlagBits::e1,
		                .tiling = vk::ImageTiling::eOptimal,
		                .usage = vk::ImageUsageFlagBits::eStorage |
		                         vk::ImageUsageFlagBits::eTransferSrc,
		                .sharingMode = vk::SharingMode::eExclusive,
		        },
			{
			.usage = VMA_MEMORY_USAGE_AUTO,
			});
	}

	// Output image views
	for (auto & plane: planes)
	{
		plane.view = device.createImageView(
		        {
		                .image = plane.image,
		                .viewType = vk::ImageViewType::e2D,
		                .format = plane.format,
		                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
		                                     .baseMipLevel = 0,
		                                     .levelCount = 1,
		                                     .baseArrayLayer = 0,
		                                     .layerCount = 1},
		        });
	}

	// Descriptor sets
	{
		std::array ds_layout_binding{
		        vk::DescriptorSetLayoutBinding{
		                .binding = 0,
		                .descriptorType = vk::DescriptorType::eStorageImage,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 1,
		                .descriptorType = vk::DescriptorType::eStorageImage,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 2,
		                .descriptorType = vk::DescriptorType::eStorageImage,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		};
		ds_layout = device.createDescriptorSetLayout({
		        .bindingCount = ds_layout_binding.size(),
		        .pBindings = ds_layout_binding.data(),
		});
	}

	// Pipeline layout
	{
		vk::PushConstantRange push_constant_range{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .offset = 0,
		        .size = sizeof(COLORSPACE_BT709),
		};
		layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &push_constant_range,
		});
	}

	// Pipeline
	{
		auto & spirv = shaders.at("yuv.comp");
		vk::raii::ShaderModule shader(device, {
		                                              .codeSize = spirv.size() * sizeof(uint32_t),
		                                              .pCode = spirv.data(),
		                                      });

		pipeline = vk::raii::Pipeline(device, nullptr, vk::ComputePipelineCreateInfo{
		                                                       .stage = {
		                                                               .stage = vk::ShaderStageFlagBits::eCompute,
		                                                               .module = *shader,
		                                                               .pName = "main",
		                                                       },
		                                                       .layout = *layout,
		                                               });
	}

	// Descriptor pool
	{
		std::array pool_size{
		        vk::DescriptorPoolSize{
		                .type = vk::DescriptorType::eSampledImage,
		                .descriptorCount = 1,
		        },
		        vk::DescriptorPoolSize{
		                .type = vk::DescriptorType::eStorageImage,
		                .descriptorCount = 2,
		        }};

		dp = device.createDescriptorPool({
		        .maxSets = 2,
		        .poolSizeCount = pool_size.size(),
		        .pPoolSizes = pool_size.data(),
		});
	}

	// Descriptor set
	ds = std::move(device.allocateDescriptorSets({
	        .descriptorPool = *dp,
	        .descriptorSetCount = 1,
	        .pSetLayouts = &*ds_layout,
	})[0]);

	vk::DescriptorImageInfo rgb_desc_image_info{
	        .imageView = *view_rgb,
	        .imageLayout = vk::ImageLayout::eGeneral,
	};
	vk::DescriptorImageInfo luma_desc_img_info_{
	        .imageView = *view_luma,
	        .imageLayout = vk::ImageLayout::eGeneral,
	};
	vk::DescriptorImageInfo chroma_desc_img_info{
	        .imageView = *view_chroma,
	        .imageLayout = vk::ImageLayout::eGeneral,
	};

	device.updateDescriptorSets(
	        {
	                vk::WriteDescriptorSet{
	                        .dstSet = *ds,
	                        .dstBinding = 0,
	                        .descriptorCount = 1,
	                        .descriptorType = vk::DescriptorType::eStorageImage,
	                        .pImageInfo = &rgb_desc_image_info,
	                },
	                vk::WriteDescriptorSet{
	                        .dstSet = *ds,
	                        .dstBinding = 1,
	                        .descriptorCount = 1,
	                        .descriptorType = vk::DescriptorType::eStorageImage,
	                        .pImageInfo = &luma_desc_img_info_,
	                },
	                vk::WriteDescriptorSet{
	                        .dstSet = *ds,
	                        .dstBinding = 2,
	                        .descriptorCount = 1,
	                        .descriptorType = vk::DescriptorType::eStorageImage,
	                        .pImageInfo = &chroma_desc_img_info,
	                },
	        },
	        nullptr);
}

void yuv_converter::record_draw_commands(
        vk::raii::CommandBuffer & cmd_buf)
{
	std::array im_barriers = {
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eNone,
	                .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eGeneral,
	                .image = luma,
	                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                                     .baseMipLevel = 0,
	                                     .levelCount = 1,
	                                     .baseArrayLayer = 0,
	                                     .layerCount = 1},
	        },
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eNone,
	                .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eGeneral,
	                .image = chroma,
	                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                                     .baseMipLevel = 0,
	                                     .levelCount = 1,
	                                     .baseArrayLayer = 0,
	                                     .layerCount = 1},
	        },
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
	                .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
	                .oldLayout = vk::ImageLayout::ePresentSrcKHR,
	                .newLayout = vk::ImageLayout::eGeneral,
	                .image = rgb,
	                .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                                     .baseMipLevel = 0,
	                                     .levelCount = 1,
	                                     .baseArrayLayer = 0,
	                                     .layerCount = 1},
	        },
	};
	cmd_buf.pipelineBarrier(
	        vk::PipelineStageFlagBits::eAllCommands,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        nullptr,
	        nullptr,
	        im_barriers);

	cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
	cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *layout, 0, *ds, {});
	cmd_buf.pushConstants<float[3][4]>(*layout, vk::ShaderStageFlagBits::eCompute, 0, COLORSPACE_BT709);
	cmd_buf.dispatch(extent.width / 16, extent.height / 16, 1);

	for (auto & barrier: im_barriers)
	{
		barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.oldLayout = vk::ImageLayout::eGeneral;
		barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
	}
	cmd_buf.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eTransfer,
	        {},
	        nullptr,
	        nullptr,
	        std::span{im_barriers.begin(), 2});
}

void yuv_converter::assemble_planes(vk::Rect2D rect, vk::raii::CommandBuffer & cmd_buf, vk::Image target)
{
	vk::ImageMemoryBarrier target_barrier{
	        .srcAccessMask = vk::AccessFlagBits::eNone,
	        .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
	        .oldLayout = vk::ImageLayout::eUndefined,
	        .newLayout = vk::ImageLayout::eTransferDstOptimal,
	        .image = target,
	        .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
	                             .baseMipLevel = 0,
	                             .levelCount = 1,
	                             .baseArrayLayer = 0,
	                             .layerCount = 1},
	};
	cmd_buf.pipelineBarrier(
	        vk::PipelineStageFlagBits::eNone,
	        vk::PipelineStageFlagBits::eTransfer,
	        {},
	        nullptr,
	        nullptr,
	        target_barrier);

	cmd_buf.copyImage(
	        luma,
	        vk::ImageLayout::eTransferSrcOptimal,
	        target,
	        vk::ImageLayout::eTransferDstOptimal,
	        vk::ImageCopy{
	                .srcSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .layerCount = 1,
	                },
	                .srcOffset = {rect.offset.x, rect.offset.y, 0},
	                .dstSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane0,
	                        .layerCount = 1,
	                },
	                .extent = {extent.width, extent.height, 1},
	        });
	cmd_buf.copyImage(
	        chroma,
	        vk::ImageLayout::eTransferSrcOptimal,
	        target,
	        vk::ImageLayout::eTransferDstOptimal,
	        vk::ImageCopy{
	                .srcSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .layerCount = 1,
	                },
	                .srcOffset = {rect.offset.x / 2, rect.offset.y / 2, 0},
	                .dstSubresource = {
	                        .aspectMask = vk::ImageAspectFlagBits::ePlane1,
	                        .layerCount = 1,
	                },
	                .extent = {extent.width / 2, extent.height / 2, 1},
	        });
}
