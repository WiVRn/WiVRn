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

#include "application.h"
#include "utils/thread_safe.h"
#include "vk/allocation.h"
#include "vk/vk_allocator.h"
#include <chrono>
#include <cstdint>
#include <ktx.h>
#include <ktxvulkan.h>
#include <memory>
#include <span>
#include <spdlog/fmt/chrono.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <tuple>
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
	image_allocation image;
	vk::raii::ImageView image_view = nullptr;
};

class KTXTexture2
{
private:
	ktxTexture2 * handle_ = nullptr;

public:
	KTXTexture2() = default;

	explicit KTXTexture2(ktxTexture2 * handle) :
	        handle_{handle} {}

	KTXTexture2(const KTXTexture2 &) = delete;
	KTXTexture2 & operator=(const KTXTexture2 &) = delete;

	KTXTexture2(KTXTexture2 && other) noexcept :
	        handle_{other.handle_}
	{
		other.handle_ = nullptr;
	}

	KTXTexture2 & operator=(KTXTexture2 && other) &
	{
		std::swap(handle_, other.handle_);
		return *this;
	}

	~KTXTexture2()
	{
		if (handle_)
		{
			ktxTexture_Destroy(ktxTexture(handle_));
			handle_ = nullptr;
		}
	}

	ktxTexture2 ** operator&()
	{
		return &handle_;
	}

	operator ktxTexture2 *()
	{
		return handle_;
	}

	ktxTexture2 * operator->() const
	{
		return handle_;
	}
};

namespace libktx_vma_glue
{
thread_safe<std::map<uint64_t, VmaAllocation>> allocations;

uint64_t add(VmaAllocation allocation)
{
	auto allocs = allocations.lock();

	uint64_t allocation_id = allocs->empty() ? 1 : (allocs->rbegin()->first + 1);
	allocs->emplace(allocation_id, allocation);
	return allocation_id;
}

VmaAllocation find(uint64_t allocation_id)
{
	auto allocs = allocations.lock();
	auto iter = allocs->find(allocation_id);
	assert(iter != allocs->end());

	return iter->second;
}

VmaAllocation release(uint64_t allocation_id)
{
	auto allocs = allocations.lock();
	auto iter = allocs->find(allocation_id);
	assert(iter != allocs->end());

	VmaAllocation allocation = iter->second;
	allocs->erase(iter);
	return allocation;
}

VmaAllocation remove(uint64_t allocation_id)
{
	auto allocs = allocations.lock();
	auto iter = allocs->find(allocation_id);
	assert(iter != allocs->end());

	auto allocation = iter->second;
	allocs->erase(iter);

	return allocation;
}

ktxVulkanTexture_subAllocatorCallbacks suballocator_callbacks{
        // Adapted from https://github.com/KhronosGroup/KTX-Software/blob/main/tests/loadtests/vkloadtests/VulkanLoadTestSample.cpp
        .allocMemFuncPtr = [](VkMemoryAllocateInfo * allocation_info, VkMemoryRequirements * memory_requirements, uint64_t * number_of_pages) -> uint64_t {
	        VmaAllocationCreateInfo info{};

	        static const vk::PhysicalDeviceMemoryProperties memory_properties = application::instance().get_physical_device().getMemoryProperties();

	        if ((memory_properties.memoryTypes[allocation_info->memoryTypeIndex].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) ||
	            (memory_properties.memoryTypes[allocation_info->memoryTypeIndex].propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent))
	        {
		        info.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
		        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	        }
	        else
	        {
		        info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	        }

	        VmaAllocation allocation;
	        VkResult result = vmaAllocateMemory(vk_allocator::instance(), memory_requirements, &info, &allocation, nullptr);
	        if (result != VK_SUCCESS)
		        return 0;

	        return add(allocation);
        },
        .bindBufferFuncPtr = [](VkBuffer buffer, uint64_t allocation_id) -> VkResult {
	        return vmaBindBufferMemory(vk_allocator::instance(), find(allocation_id), buffer);
        },
        .bindImageFuncPtr = [](VkImage image, uint64_t allocation_id) -> VkResult {
	        return vmaBindImageMemory(vk_allocator::instance(), find(allocation_id), image);
        },
        .memoryMapFuncPtr = [](uint64_t allocation_id, uint64_t, VkDeviceSize * map_length, void ** data) -> VkResult {
	        auto allocation = find(allocation_id);

	        VmaAllocationInfo info{};
	        vmaGetAllocationInfo(vk_allocator::instance(), allocation, &info);

	        *map_length = info.size;
	        return vmaMapMemory(vk_allocator::instance(), allocation, data);
        },
        .memoryUnmapFuncPtr = [](uint64_t allocation_id, uint64_t) { vmaUnmapMemory(vk_allocator::instance(), find(allocation_id)); },
        .freeMemFuncPtr = [](uint64_t allocation_id) { vmaFreeMemory(vk_allocator::instance(), remove(allocation_id)); },
};
} // namespace libktx_vma_glue

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

image_loader::image_loader(vk::raii::Device & device, vk::raii::PhysicalDevice physical_device, thread_safe<vk::raii::Queue> & queue, uint32_t queue_family_index) :
        device(device),
        queue(queue),
        cb_pool(device,
                vk::CommandPoolCreateInfo{
                        .flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                        .queueFamilyIndex = queue_family_index,
                })
{
	// Assume we will always use the same queue
	static thread_safe<vk::raii::Queue> * q = &queue;

	ktxVulkanFunctions vulkan_functions{
	        .vkGetInstanceProcAddr = &vkGetInstanceProcAddr,
	        .vkGetDeviceProcAddr = &vkGetDeviceProcAddr,
	        .vkQueueSubmit = [](VkQueue queue,
	                            uint32_t submitCount,
	                            const VkSubmitInfo * pSubmits,
	                            VkFence fence) -> VkResult {
		        assert(q and (VkQueue) * q->get_unsafe() == queue);
		        auto _ = q->lock();

		        return vkQueueSubmit(queue, submitCount, pSubmits, fence);
	        },
	        .vkQueueWaitIdle = [](VkQueue queue) -> VkResult {
		        // libktx only uses vkQueueWaitIdle when the tiling is not VK_IMAGE_TILING_OPTIMAL
		        abort();
	        },
	};

	ktxVulkanDeviceInfo_ConstructEx(
	        &vdi,
	        *application::get_vulkan_instance(),
	        *physical_device,
	        *device,
	        *queue.get_unsafe(),
	        *cb_pool,
	        nullptr, // Allocation callbacks
	        &vulkan_functions);

	std::array<std::tuple<vk::Format, ktx_transcode_fmt_e, bool>, 6> formats{{
	        {vk::Format::eAstc4x4SrgbBlock, KTX_TTF_ASTC_4x4_RGBA, true},
	        {vk::Format::eAstc4x4UnormBlock, KTX_TTF_ASTC_4x4_RGBA, false},
	        {vk::Format::eBc7SrgbBlock, KTX_TTF_BC7_RGBA, true},
	        {vk::Format::eBc7UnormBlock, KTX_TTF_BC7_RGBA, false},
	        {vk::Format::eR8G8B8A8Srgb, KTX_TTF_RGBA32, true},
	        {vk::Format::eR8G8B8Unorm, KTX_TTF_RGBA32, false},
	}};

	for (auto [vk_format, ktx_format, srgb]: formats)
	{
		auto prop = physical_device.getFormatProperties(vk_format);

		if (prop.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage)
		{
			if (srgb)
				supported_srgb_formats.emplace_back(vk_format, ktx_format);
			else
				supported_linear_formats.emplace_back(vk_format, ktx_format);
		}
	}
}

image_loader::~image_loader()
{
	ktxVulkanDeviceInfo_Destruct(&vdi);
}

template <typename T>
float alpha_scale;
template <>
float alpha_scale<uint8_t> = 1. / 255.;
template <>
float alpha_scale<uint16_t> = 1. / 65535.;
template <>
float alpha_scale<float> = 1.;

template <typename T>
void premultiply_alpha_aux(T * destination, const T * source, int n)
{
	for (int i = 0; i < n; i++)
	{
		float alpha = source[4 * i + 3] * alpha_scale<T>;

		destination[4 * i + 0] = source[4 * i + 0] * alpha;
		destination[4 * i + 1] = source[4 * i + 1] * alpha;
		destination[4 * i + 2] = source[4 * i + 2] * alpha;
		destination[4 * i + 3] = source[4 * i + 3];
	}
}

static void premultiply_alpha(void * destination, const void * source, vk::Extent3D extent, vk::Format format)
{
	switch (format)
	{
		case vk::Format::eR8G8B8A8Srgb: // TODO: sRGB
		case vk::Format::eR8G8B8A8Unorm:
			premultiply_alpha_aux<uint8_t>((uint8_t *)destination, (const uint8_t *)source, extent.width * extent.height * extent.depth);
			break;

		case vk::Format::eR16G16B16A16Unorm:
			premultiply_alpha_aux<uint16_t>((uint16_t *)destination, (const uint16_t *)source, extent.width * extent.height * extent.depth);
			break;

		case vk::Format::eR32G32B32A32Sfloat:
			premultiply_alpha_aux<float>((float *)destination, (const float *)source, extent.width * extent.height * extent.depth);
			break;

		default:
			break;
	}
}

loaded_image image_loader::do_load_raw(const void * pixels, vk::Extent3D extent, vk::Format format, const std::string & name, bool premultiply)
{
	auto cb = std::move(device.allocateCommandBuffers({
	        .commandPool = *cb_pool,
	        .level = vk::CommandBufferLevel::ePrimary,
	        .commandBufferCount = 1,
	})[0]);

	cb.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	assert(format != vk::Format::eUndefined);

	uint32_t num_mipmaps = std::floor(std::log2(std::max(extent.width, extent.height))) + 1;

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
	        name + " (staging)"};

	if (premultiply)
		premultiply_alpha(staging_buffer.map(), pixels, extent, format);
	else
		memcpy(staging_buffer.map(), pixels, byte_size);

	staging_buffer.unmap();

	// Allocate image
	image_allocation image{
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
	        name};

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
	cb.copyBufferToImage(
	        staging_buffer,
	        image,
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
		cb.blitImage(
		        image,
		        vk::ImageLayout::eTransferSrcOptimal,
		        image,
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
	                .image = image,
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
	queue.lock()->submit(info, *fence);
	if (auto result = device.waitForFences(*fence, true, 1'000'000'000); result != vk::Result::eSuccess)
		throw std::runtime_error("vkWaitForfences: " + vk::to_string(result));

	vk::ImageViewType image_view_type;
	if (extent.depth > 1)
		image_view_type = vk::ImageViewType::e3D;
	else
		image_view_type = vk::ImageViewType::e2D;

	vk::raii::ImageView image_view{
	        device,
	        vk::ImageViewCreateInfo{
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
	        },
	};

	return loaded_image{
	        .image = std::move(image),
	        .image_view = std::move(image_view),
	        .format = format,
	        .extent = extent,
	        .num_mipmaps = num_mipmaps,
	        .image_view_type = image_view_type,
	        .is_alpha_premultiplied = premultiply,
	};
}

loaded_image image_loader::do_load_ktx(std::span<const std::byte> bytes, bool srgb, const std::string & name, const std::filesystem::path & output_file)
{
	KTXTexture2 texture{};
	// KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT checks that the file is compatible with the KHR_texture_basisu glTF extension
	// ktxResult err = ktxTexture2_CreateFromMemory(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
	ktxResult err = ktxTexture2_CreateFromMemory(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), 0, &texture);
	// ktxResult err = ktxTexture2_CreateFromMemory(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT | KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);

	if (err != KTX_SUCCESS)
	{
		spdlog::warn("ktxTexture2_CreateFromMemory: error {}", ktxErrorString(err));
		throw std::runtime_error("ktxTexture2_CreateFromMemory");
	}

	if (ktxTexture2_NeedsTranscoding(texture))
	{
		auto format = srgb ? supported_srgb_formats.front() : supported_linear_formats.front();

		if (ktxTexture2_TranscodeBasis(texture, format.second, 0) != KTX_SUCCESS)
		{
			spdlog::warn("ktxTexture2_TranscodeBasis: error {}", ktxErrorString(err));
			throw std::runtime_error("ktxTexture2_TranscodeBasis");
		}

		if (output_file != "")
		{
			spdlog::debug("Saving transcoded texture to {}", output_file.native());
			const char writer[] = "WiVRn";
			ktxHashList_DeleteKVPair(&texture->kvDataHead, KTX_WRITER_KEY);
			ktxHashList_AddKVPair(&texture->kvDataHead, KTX_WRITER_KEY, sizeof(writer), writer);

			err = ktxTexture2_WriteToNamedFile(texture, output_file.c_str());
			if (err != KTX_SUCCESS)
			{
				spdlog::warn("ktxTexture2_WriteToNamedFile: error {}", ktxErrorString(err));
			}
		}
	}

	ktxVulkanTexture vk_texture;
	{
		err = ktxTexture2_VkUploadEx_WithSuballocator(texture, &vdi, &vk_texture, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, &libktx_vma_glue::suballocator_callbacks);
	}

	if (err != KTX_SUCCESS)
	{
		spdlog::warn("ktxTexture2_VkUploadEx: {}", ktxErrorString(err));
		throw std::runtime_error("ktxTexture2_VkUploadEx");
	}

	return loaded_image{
	        .image{libktx_vma_glue::release(vk_texture.allocationId), device, vk_texture.image, name}, // Take over ownership of vk_texture, do not call ktxTexture2_destruct
	        .image_view{
	                device,
	                vk::ImageViewCreateInfo{
	                        .image = vk_texture.image,
	                        .viewType = vk::ImageViewType(vk_texture.viewType),
	                        .format = vk::Format(vk_texture.imageFormat),
	                        .subresourceRange = {
	                                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                                .baseMipLevel = 0,
	                                .levelCount = texture->numLevels,
	                                .baseArrayLayer = 0,
	                                .layerCount = 1,
	                        },
	                }},
	        .format = vk::Format(vk_texture.imageFormat),
	        .extent{vk::Extent3D(texture->baseWidth, texture->baseHeight, texture->baseDepth)},
	        .num_mipmaps = texture->numLevels,
	        .image_view_type = vk::ImageViewType(vk_texture.viewType),
	        .is_alpha_premultiplied = ktxTexture2_GetPremultipliedAlpha(texture),
	};
}

static constexpr bool starts_with(std::span<const std::byte> data, std::span<const uint8_t> prefix)
{
	return data.size() >= prefix.size() && !memcmp(data.data(), prefix.data(), prefix.size());
}

loaded_image image_loader::do_load_image(std::span<const std::byte> bytes, bool srgb, const std::string & name, bool premultiply)
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

	vk::Format format;
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

	vk::Extent3D extent{
	        .width = uint32_t(w),
	        .height = uint32_t(h),
	        .depth = 1,
	};

	return do_load_raw(pixels.get(), extent, format, name, premultiply);
}

// Load a PNG/JPEG/KTX2 file
loaded_image image_loader::load(std::span<const std::byte> bytes, bool srgb, const std::string & name, bool premultiply, const std::filesystem::path & output_file)
{
	const uint8_t ktx1_magic[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
	const uint8_t ktx2_magic[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

	if (starts_with(bytes, ktx1_magic) || starts_with(bytes, ktx2_magic))
		return do_load_ktx(bytes, srgb, name, output_file);
	else
		return do_load_image(bytes, srgb, name, premultiply);
}

// Load raw pixel data
loaded_image image_loader::load(const void * pixels, size_t size, vk::Extent3D extent, vk::Format format, const std::string & name, bool premultiply)
{
	if (size < extent.width * extent.height * extent.depth * bytes_per_pixel(format))
		throw std::invalid_argument("size");

	return do_load_raw(pixels, extent, format, name, premultiply);
}
