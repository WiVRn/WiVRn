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
#include "wivrn_config.h"

#include <cstdint>
#if WIVRN_USE_LIBKTX
#include <ktxvulkan.h>
#endif
#include <memory>
#include <span>
#include <spdlog/spdlog.h>
#include <stdexcept>
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
struct image_resources
{
	image_allocation allocation;
	vk::Image image;
	vk::raii::ImageView image_view = nullptr;
};

#if WIVRN_USE_LIBKTX
struct ktx_image_resources
{
	ktxVulkanTexture ktx_texture;
	VkDevice device;
	vk::Image image;
	vk::raii::ImageView image_view = nullptr;

	ktx_image_resources() = default;
	ktx_image_resources(const ktx_image_resources &) = delete;
	ktx_image_resources & operator=(const ktx_image_resources &) = delete;
	~ktx_image_resources()
	{
		ktxVulkanTexture_Destruct(&ktx_texture, device, nullptr);
	}
};
#endif

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

image_loader::image_loader(vk::raii::PhysicalDevice physical_device, vk::raii::Device & device, vk::raii::Queue & queue, vk::raii::CommandPool & cb_pool) :
        device(device),
        queue(queue),
        cb_pool(cb_pool)
{
#if WIVRN_USE_LIBKTX
	vdi = ktxVulkanDeviceInfo_Create(*physical_device, *device, *queue, *cb_pool, nullptr);
#endif
}

image_loader::~image_loader()
{
#if WIVRN_USE_LIBKTX
	if (vdi)
		ktxVulkanDeviceInfo_Destroy(vdi);
#endif
}

void image_loader::do_load_raw(const void * pixels, vk::Extent3D extent, vk::Format format)
{
	auto cb = std::move(device.allocateCommandBuffers({
	        .commandPool = *cb_pool,
	        .level = vk::CommandBufferLevel::ePrimary,
	        .commandBufferCount = 1,
	})[0]);

	cb.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	assert(format != vk::Format::eUndefined);

	this->format = format;
	this->extent = extent;
	if (extent.depth > 1)
		image_view_type = vk::ImageViewType::e3D;
	else
		image_view_type = vk::ImageViewType::e2D;

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
	auto r = std::make_shared<image_resources>();
	r->allocation = image_allocation{
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
	        },
	        "image_loader::do_load"};

	r->image = (vk::Image)r->allocation;

	// Transition all mipmap levels layout to eTransferDstOptimal
	cb.pipelineBarrier(
	        vk::PipelineStageFlagBits::eTransfer,
	        vk::PipelineStageFlagBits::eTransfer,
	        vk::DependencyFlags{},
	        {},
	        {},
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eNone,
	                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eTransferDstOptimal,
	                .image = r->image,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .baseMipLevel = 0,
	                        .levelCount = num_mipmaps,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	        });

	// Copy image data
	cb.copyBufferToImage(
	        staging_buffer,
	        r->image,
	        vk::ImageLayout::eTransferDstOptimal,
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
		cb.pipelineBarrier(
		        vk::PipelineStageFlagBits::eTransfer,
		        vk::PipelineStageFlagBits::eFragmentShader,
		        vk::DependencyFlags{},
		        {},
		        {},
		        vk::ImageMemoryBarrier{
		                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
		                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
		                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
		                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
		                .image = r->image,
		                .subresourceRange = {
		                        .aspectMask = vk::ImageAspectFlagBits::eColor,
		                        .baseMipLevel = level - 1,
		                        .levelCount = 1,
		                        .baseArrayLayer = 0,
		                        .layerCount = 1,
		                },
		        });

		// Blit level n-1 to level n
		cb.blitImage(
		        r->image,
		        vk::ImageLayout::eTransferSrcOptimal,
		        r->image,
		        vk::ImageLayout::eTransferDstOptimal,
		        vk::ImageBlit{
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
		cb.pipelineBarrier(
		        vk::PipelineStageFlagBits::eTransfer,
		        vk::PipelineStageFlagBits::eFragmentShader,
		        vk::DependencyFlags{},
		        {},
		        {},
		        vk::ImageMemoryBarrier{
		                .srcAccessMask = vk::AccessFlagBits::eTransferRead,
		                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
		                .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
		                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		                .image = r->image,
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
	cb.pipelineBarrier(
	        vk::PipelineStageFlagBits::eTransfer,
	        vk::PipelineStageFlagBits::eFragmentShader,
	        vk::DependencyFlags{},
	        {},
	        {},
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
	                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
	                .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	                .image = r->image,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .baseMipLevel = num_mipmaps - 1,
	                        .levelCount = 1,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	        });

	cb.end();
	vk::SubmitInfo info;
	info.setCommandBuffers(*cb);
	auto fence = device.createFence(vk::FenceCreateInfo{});
	queue.submit(info, *fence);
	if (auto result = device.waitForFences(*fence, true, 1'000'000'000); result != vk::Result::eSuccess)
		throw std::runtime_error("vkWaitForfences: " + vk::to_string(result));

	r->image_view = vk::raii::ImageView{
	        device,
	        vk::ImageViewCreateInfo{
	                .image = r->image,
	                .viewType = image_view_type,
	                .format = format,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .baseMipLevel = 0,
	                        .levelCount = num_mipmaps,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	        },
	};

	image_view = std::shared_ptr<vk::raii::ImageView>(r, &r->image_view);
}

void image_loader::do_load_ktx(std::span<const std::byte> bytes)
{
#if WIVRN_USE_LIBKTX
	ktxTexture * texture;
	ktxVulkanTexture vk_texture;

	ktxResult err = ktxTexture_CreateFromMemory(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), /*KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT*/ 0, &texture);

	if (err != KTX_SUCCESS)
	{
		spdlog::info("ktxTexture_CreateFromMemory: error {}", (int)err);
		throw std::runtime_error("ktxTexture_CreateFromMemory");
	}

	if (ktxTexture_NeedsTranscoding(texture))
	{
		// TODO
	}

	err = ktxTexture_VkUploadEx(texture, vdi, &vk_texture, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	if (err != KTX_SUCCESS)
	{
		// TODO try uncompressing the texture
		ktxTexture_Destroy(texture);
		spdlog::info("ktxTexture_CreateFromMemory: error {}", (int)err);
		throw std::runtime_error("ktxTexture_VkUploadEx");
	}

	format = vk::Format(vk_texture.imageFormat);
	extent = vk::Extent3D(texture->baseWidth, texture->baseHeight, texture->baseDepth);
	image_view_type = vk::ImageViewType(vk_texture.viewType);
	num_mipmaps = texture->numLevels;

	ktxTexture_Destroy(texture);

	auto r = std::make_shared<ktx_image_resources>();

	r->ktx_texture = vk_texture;
	r->device = *device;
	r->image = vk::Image(vk_texture.image);
	r->image_view = vk::raii::ImageView{
	        device,
	        vk::ImageViewCreateInfo{
	                .image = r->image,
	                .viewType = image_view_type,
	                .format = format,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .baseMipLevel = 0,
	                        .levelCount = num_mipmaps,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	        },
	};

	image_view = std::shared_ptr<vk::raii::ImageView>(r, &r->image_view);
#else
	throw std::runtime_error("Compiled without KTX support");
#endif
}

static constexpr bool starts_with(std::span<const std::byte> data, std::span<const uint8_t> prefix)
{
	return data.size() >= prefix.size() && !memcmp(data.data(), prefix.data(), prefix.size());
}

template <typename T>
void premultiply_alpha_aux(T * pixels, float alpha_scale, int width, int height)
{
	for (int i = 0, n = width * height; i < n; i++)
	{
		float alpha = pixels[4 * i + 3] * alpha_scale;

		pixels[4 * i + 0] = pixels[4 * i + 0] * alpha;
		pixels[4 * i + 1] = pixels[4 * i + 1] * alpha;
		pixels[4 * i + 2] = pixels[4 * i + 2] * alpha;
	}
}

static void premultiply_alpha(void * pixels, vk::Extent3D extent, vk::Format format)
{
	switch (format)
	{
		case vk::Format::eR8G8B8A8Srgb: // TODO: sRGB
		case vk::Format::eR8G8B8A8Unorm:
			premultiply_alpha_aux<uint8_t>((uint8_t *)pixels, 1. / 255., extent.width, extent.height);
			break;

		case vk::Format::eR16G16B16A16Unorm:
			premultiply_alpha_aux<uint16_t>((uint16_t *)pixels, 1. / 65535., extent.width, extent.height);
			break;

		case vk::Format::eR32G32B32A32Sfloat:
			premultiply_alpha_aux<float>((float *)pixels, 1, extent.width, extent.height);
			break;

		default:
			break;
	}
}

// Load a PNG/JPEG/KTX2 file
void image_loader::load(std::span<const std::byte> bytes, bool srgb)
{
	const uint8_t ktx1_magic[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
	const uint8_t ktx2_magic[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

	if (starts_with(bytes, ktx1_magic) || starts_with(bytes, ktx2_magic))
	{
		do_load_ktx(bytes);
	}
	else
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

		premultiply_alpha(pixels.get(), extent, format);

		do_load_raw(pixels.get(), extent, format);
	}
}

// Load raw pixel data
void image_loader::load(const void * pixels, size_t size, vk::Extent3D extent_, vk::Format format_)
{
	extent = extent_;
	format = format_;
	image_view_type = vk::ImageViewType::e2D;

	if (size < extent.width * extent.height * extent.depth * bytes_per_pixel(format))
		throw std::invalid_argument("size");

	do_load_raw(pixels, extent_, format_);
}
