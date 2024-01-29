/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "image_loader.h"

#include "vulkan/vulkan_enums.hpp"
#include "vulkan/vulkan_handles.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vulkan/vulkan_raii.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG

#ifdef __ARM_NEON
#define STBI_NEON
#endif

#include "stb_image.h"

using stbi_ptr = std::unique_ptr<void, decltype([](void * pixels) { stbi_image_free(pixels); })>;

namespace
{
template <typename T>
constexpr vk::Format formats[4] = {vk::Format::eUndefined, vk::Format::eUndefined, vk::Format::eUndefined, vk::Format::eUndefined};

template <>
constexpr vk::Format formats<uint8_t>[] = {
        vk::Format::eR8Unorm,
        vk::Format::eR8G8Unorm,
        vk::Format::eUndefined,
        vk::Format::eR8G8B8A8Unorm,
};

template <>
constexpr vk::Format formats<uint16_t>[] = {
        vk::Format::eR16Unorm,
        vk::Format::eR16G16Unorm,
        vk::Format::eUndefined,
        vk::Format::eR16G16B16A16Unorm,
};

template <>
constexpr vk::Format formats<float>[] = {
        vk::Format::eR32Sfloat,
        vk::Format::eR32G32Sfloat,
        vk::Format::eUndefined,
        vk::Format::eR32G32B32A32Sfloat,
};

template <typename T>
vk::Format get_format(int num_components)
{
	assert(num_components > 0 && num_components <= 4);

	return formats<T>[num_components - 1];
}

vk::Format get_format_srgb(int num_components)
{
	switch (num_components)
	{
		case 1:
			return vk::Format::eR8Srgb;
		case 2:
			return vk::Format::eR8G8Srgb;
		case 3:
			return vk::Format::eUndefined;
		case 4:
			return vk::Format::eR8G8B8A8Srgb;

		default:
			assert(false);
	}

	__builtin_unreachable();
}

int bytes_per_pixel(vk::Format format)
{
	switch (format)
	{
		case vk::Format::eR8Srgb:
		case vk::Format::eR8Unorm:
			return 1;
		case vk::Format::eR8G8Srgb:
		case vk::Format::eR8G8Unorm:
			return 2;
		case vk::Format::eR8G8B8A8Srgb:
		case vk::Format::eR8G8B8A8Unorm:
			return 4;

		case vk::Format::eR16Unorm:
			return 2;
		case vk::Format::eR16G16Unorm:
			return 4;
		case vk::Format::eR16G16B16A16Unorm:
			return 8;

		case vk::Format::eR32Sfloat:
			return 4;
		case vk::Format::eR32G32Sfloat:
			return 8;
		case vk::Format::eR32G32B32A32Sfloat:
			return 16;

		default:
			assert(false);
	}

	__builtin_unreachable();
}
} // namespace

void image_loader::do_load(vk::raii::Device & device, vk::raii::CommandBuffer & cb, const void * pixels, vk::Extent3D extent, vk::Format format)
{
	assert(format != vk::Format::eUndefined);

	this->format = format;
	this->extent = extent;
	if (extent.depth > 1)
	{
		image_type = vk::ImageType::e3D;
		image_view_type = vk::ImageViewType::e3D;
	}
	else
	{
		image_type = vk::ImageType::e2D;
		image_view_type = vk::ImageViewType::e2D;
	}
	num_mipmaps = std::floor(std::log2(std::max(extent.width, extent.height))) + 1;

	size_t byte_size = extent.width * extent.height * extent.depth * bytes_per_pixel(format);

	// Copy to staging buffer
	staging_buffer = buffer_allocation{
	        device,
	        vk::BufferCreateInfo{
	                .size = byte_size,
	                .usage = vk::BufferUsageFlagBits::eTransferSrc},
	        VmaAllocationCreateInfo{
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        "image_loader::do_load (staging)"};

	memcpy(staging_buffer.map(), pixels, byte_size);
	staging_buffer.unmap();

	// Allocate image
	image = image_allocation{
	        device,
	        vk::ImageCreateInfo{
	                .imageType = vk::ImageType::e2D,
	                .format = format,
	                .extent = extent,
	                .mipLevels = num_mipmaps,
	                .arrayLayers = 1,
	                .samples = vk::SampleCountFlagBits::e1,
	                .tiling = vk::ImageTiling::eOptimal,
	                .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
	                .initialLayout = vk::ImageLayout::eUndefined},
	        VmaAllocationCreateInfo{
	                .flags = 0,
	                .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	        }, "image_loader::do_load"};

	// Transition all mipmap levels layout to eTransferDstOptimal
	cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags{}, {}, {}, vk::ImageMemoryBarrier{
	                                                                                                                                      .srcAccessMask = vk::AccessFlagBits::eNone,
	                                                                                                                                      .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
	                                                                                                                                      .oldLayout = vk::ImageLayout::eUndefined,
	                                                                                                                                      .newLayout = vk::ImageLayout::eTransferDstOptimal,
	                                                                                                                                      .image = image,
	                                                                                                                                      .subresourceRange = {
	                                                                                                                                              .aspectMask = vk::ImageAspectFlagBits::eColor,
	                                                                                                                                              .baseMipLevel = 0,
	                                                                                                                                              .levelCount = num_mipmaps,
	                                                                                                                                              .baseArrayLayer = 0,
	                                                                                                                                              .layerCount = 1,
	                                                                                                                                      },
	                                                                                                                              });

	// Copy image data
	cb.copyBufferToImage(staging_buffer, image, vk::ImageLayout::eTransferDstOptimal, vk::BufferImageCopy{
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
	                                                                                          .imageExtent = extent,
	                                                                                  });

	// Create mipmaps
	int width = extent.width;
	int height = extent.height;
	for (uint32_t level = 1; level < num_mipmaps; level++)
	{
		int next_width = width > 1 ? width / 2 : 1;
		int next_height = height > 1 ? height / 2 : 1;

		// Transition source image layout to eTransferSrcOptimal
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags{}, {}, {}, vk::ImageMemoryBarrier{
		                                                                                                                                            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
		                                                                                                                                            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
		                                                                                                                                            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
		                                                                                                                                            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
		                                                                                                                                            .image = image,
		                                                                                                                                            .subresourceRange = {
		                                                                                                                                                    .aspectMask = vk::ImageAspectFlagBits::eColor,
		                                                                                                                                                    .baseMipLevel = level - 1,
		                                                                                                                                                    .levelCount = 1,
		                                                                                                                                                    .baseArrayLayer = 0,
		                                                                                                                                                    .layerCount = 1,
		                                                                                                                                            },
		                                                                                                                                    });

		// Blit level n-1 to level n
		cb.blitImage(image, vk::ImageLayout::eTransferSrcOptimal, image, vk::ImageLayout::eTransferDstOptimal, vk::ImageBlit{
		                                                                                                               .srcSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = level - 1, .baseArrayLayer = 0, .layerCount = 1},
		                                                                                                               .srcOffsets = std::array{
		                                                                                                                       vk::Offset3D{0, 0, 0},
		                                                                                                                       vk::Offset3D{width, height, 1},
		                                                                                                               },
		                                                                                                               .dstSubresource = {.aspectMask = vk::ImageAspectFlagBits::eColor, .mipLevel = level, .baseArrayLayer = 0, .layerCount = 1},
		                                                                                                               .dstOffsets = std::array{
		                                                                                                                       vk::Offset3D{0, 0, 0},
		                                                                                                                       vk::Offset3D{next_width, next_height, 1},
		                                                                                                               },
		                                                                                                       },
		             vk::Filter::eLinear);

		// Transition source image layout to eShaderReadOnlyOptimal
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags{}, {}, {}, vk::ImageMemoryBarrier{
		                                                                                                                                            .srcAccessMask = vk::AccessFlagBits::eTransferRead,
		                                                                                                                                            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
		                                                                                                                                            .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
		                                                                                                                                            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		                                                                                                                                            .image = image,
		                                                                                                                                            .subresourceRange = {
		                                                                                                                                                    .aspectMask = vk::ImageAspectFlagBits::eColor,
		                                                                                                                                                    .baseMipLevel = level - 1,
		                                                                                                                                                    .levelCount = 1,
		                                                                                                                                                    .baseArrayLayer = 0,
		                                                                                                                                                    .layerCount = 1,
		                                                                                                                                            },
		                                                                                                                                    });

		width = next_width;
		height = next_height;
	}

	// Transition the last level to eShaderReadOnlyOptimal
	cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlags{}, {}, {}, vk::ImageMemoryBarrier{
	                                                                                                                                            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	                                                                                                                                            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
	                                                                                                                                            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
	                                                                                                                                            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	                                                                                                                                            .image = image,
	                                                                                                                                            .subresourceRange = {
	                                                                                                                                                    .aspectMask = vk::ImageAspectFlagBits::eColor,
	                                                                                                                                                    .baseMipLevel = num_mipmaps - 1,
	                                                                                                                                                    .levelCount = 1,
	                                                                                                                                                    .baseArrayLayer = 0,
	                                                                                                                                                    .layerCount = 1,
	                                                                                                                                            },
	                                                                                                                                    });

	// Create the image view
	image_view = vk::raii::ImageView{device, vk::ImageViewCreateInfo{
	                                                 .image = image,
	                                                 .viewType = image_view_type,
	                                                 .format = format,
	                                                 .subresourceRange = {
	                                                         .aspectMask = vk::ImageAspectFlagBits::eColor,
	                                                         .baseMipLevel = 0,
	                                                         .levelCount = num_mipmaps,
	                                                         .baseArrayLayer = 0,
	                                                         .layerCount = 1,
	                                                 },
	                                         }};
}

image_loader::image_loader(vk::raii::Device & device, vk::raii::CommandBuffer & cb, const void * pixels, vk::Extent3D extent, vk::Format format)
{
	do_load(device, cb, pixels, extent, format);
}

image_loader::image_loader(vk::raii::Device & device, vk::raii::CommandBuffer & cb, std::span<const std::byte> bytes, bool srgb)
{
	const stbi_uc * image_data = (const stbi_uc *)bytes.data();
	size_t image_size = bytes.size();

	int w, h, num_channels, channels_in_file;
	stbi_ptr pixels;

	if (!stbi_info_from_memory(image_data, image_size, &w, &h, &num_channels))
		throw std::runtime_error("Unsupported image format");

	assert(num_channels >= 1 && num_channels <= 4);

	if (num_channels == 3)
		num_channels = 4;

	if (stbi_is_hdr_from_memory(image_data, image_size))
	{
		pixels = stbi_ptr(stbi_loadf_from_memory(image_data, image_size, &w, &h, &channels_in_file, num_channels));
		format = get_format<float>(num_channels);
	}
	else if (stbi_is_16_bit_from_memory(image_data, image_size))
	{
		pixels = stbi_ptr(stbi_load_16_from_memory(image_data, image_size, &w, &h, &channels_in_file, num_channels));
		format = get_format<uint16_t>(num_channels);
	}
	else
	{
		pixels = stbi_ptr(stbi_load_from_memory(image_data, image_size, &w, &h, &channels_in_file, num_channels));
		format = srgb ? get_format_srgb(num_channels) : get_format<uint8_t>(num_channels);
	}

	extent.width = w;
	extent.height = h;
	extent.depth = 1;

	do_load(device, cb, pixels.get(), extent, format);
}
