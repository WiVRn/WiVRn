// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#include "pyrowave_common.h"
#include <map>

#if PYROWAVE_PRECISION < 0 || PYROWAVE_PRECISION > 2
#error "PYROWAVE_PRECISION must be in range [0, 2]."
#endif

constexpr int WaveletFP16Levels = 2;

namespace pyrowave
{
extern const std::map<std::string, std::vector<uint32_t>> shaders;
}

namespace PyroWave
{

vk::raii::ShaderModule load_shader(vk::raii::Device & device, const std::string & name)
{
	const auto & spirv = pyrowave::shaders.at(name);
	vk::ShaderModuleCreateInfo create_info{
	        .codeSize = spirv.size() * sizeof(uint32_t),
	        .pCode = data(spirv),
	};

	return vk::raii::ShaderModule{device, create_info};
}

bool supports_subgroup_size_log2(
        vk::PhysicalDeviceVulkan13Properties const & prop13,
        bool subgroup_full_group,
        uint8_t subgroup_minimum_size_log2,
        uint8_t subgroup_maximum_size_log2,
        vk::ShaderStageFlagBits stage)
{
	uint32_t min_subgroups = 1u << subgroup_minimum_size_log2;
	uint32_t max_subgroups = 1u << subgroup_maximum_size_log2;

	bool full_range = min_subgroups <= prop13.minSubgroupSize &&
	                  max_subgroups >= prop13.maxSubgroupSize;

	// We can use VARYING size.
	if (full_range)
		return true;

	if (min_subgroups > prop13.maxSubgroupSize ||
	    max_subgroups < prop13.minSubgroupSize)
	{
		// No overlap in requested subgroup size and available subgroup size.
		return false;
	}

	// We need requiredSubgroupSizeStages support here.
	return (prop13.requiredSubgroupSizeStages & stage) != vk::ShaderStageFlags{};
}

Configuration::Configuration()
{
	precision = PYROWAVE_PRECISION;
	if (const char * env = getenv("PYROWAVE_PRECISION"))
		precision = int(strtol(env, nullptr, 0));

	if (precision < 0 || precision > 2)
	{
		fprintf(stderr, "pyrowave: precision must be in range [0, 2].\n");
		precision = PYROWAVE_PRECISION;
	}
}

Configuration & Configuration::get()
{
	static Configuration config;
	return config;
}

int Configuration::get_precision() const
{
	return precision;
}

void WaveletBuffers::accumulate_block_mapping(int blocks_x_8x8, int blocks_y_8x8)
{
	int blocks_x_32x32 = (blocks_x_8x8 + 3) / 4;
	int blocks_y_32x32 = (blocks_y_8x8 + 3) / 4;

	for (int y = 0; y < blocks_y_32x32; y++)
	{
		for (int x = 0; x < blocks_x_32x32; x++)
		{
			BlockMapping mapping = {};
			mapping.block_offset_8x8 = block_count_8x8 + 4 * y * blocks_x_8x8 + 4 * x;
			mapping.block_stride_8x8 = blocks_x_8x8;
			mapping.block_width_8x8 = std::min<int>(4, blocks_x_8x8 - 4 * x);
			mapping.block_height_8x8 = std::min<int>(4, blocks_y_8x8 - 4 * y);
			block_32x32_to_8x8_mapping.push_back(mapping);
			block_count_32x32++;
		}
	}

	block_count_8x8 += blocks_x_8x8 * blocks_y_8x8;
}

void WaveletBuffers::init_block_meta()
{
	for (int level = DecompositionLevels - 1; level >= 0; level--)
	{
		for (int component = 0; component < NumComponents; component++)
		{
			// Ignore top-level CbCr when doing 420 subsampling.
			if (level == 0 && component != 0 && chroma == ChromaSubsampling::Chroma420)
				continue;

			for (int band = (level == DecompositionLevels - 1 ? 0 : 1); band < 4; band++)
			{
				uint32_t level_width = get_width(wavelet_img_high_res, level);
				uint32_t level_height = get_height(wavelet_img_high_res, level);

				int blocks_x_8x8 = (level_width + 7) / 8;
				int blocks_y_8x8 = (level_height + 7) / 8;
				int blocks_x_32x32 = (level_width + 31) / 32;

				block_meta[component][level][band] = {
				        block_count_8x8,
				        blocks_x_8x8,
				        block_count_32x32,
				        blocks_x_32x32,
				};

				accumulate_block_mapping(blocks_x_8x8, blocks_y_8x8);
			}
		}
	}
}

WaveletBuffers::WaveletBuffers(vk::raii::Device & device, int width, int height, ChromaSubsampling chroma) :
        device(device),

        mirror_repeat_sampler(
                device,
                vk::SamplerCreateInfo{
                        .magFilter = vk::Filter::eNearest,
                        .minFilter = vk::Filter::eNearest,
                        .mipmapMode = vk::SamplerMipmapMode::eNearest,
                        .addressModeU = vk::SamplerAddressMode::eMirroredRepeat,
                        .addressModeV = vk::SamplerAddressMode::eMirroredRepeat,
                        .addressModeW = vk::SamplerAddressMode::eMirroredRepeat,
                }),

        border_sampler(
                device,
                vk::SamplerCreateInfo{
                        .magFilter = vk::Filter::eNearest,
                        .minFilter = vk::Filter::eNearest,
                        .mipmapMode = vk::SamplerMipmapMode::eNearest,
                        .addressModeU = vk::SamplerAddressMode::eClampToBorder,
                        .addressModeV = vk::SamplerAddressMode::eClampToBorder,
                        .addressModeW = vk::SamplerAddressMode::eClampToBorder,
                        .borderColor = vk::BorderColor::eFloatTransparentBlack,
                }),

        component_layer_views{{nullptr, nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr, nullptr}},
        component_ll_views{{nullptr, nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr, nullptr}, {nullptr, nullptr, nullptr, nullptr, nullptr}},

        width(width),
        height(height),
        aligned_width(std::max(align(width, Alignment), MinimumImageSize)),
        aligned_height(std::max(align(height, Alignment), MinimumImageSize)),
        chroma(chroma)
{
	vk::ImageCreateInfo info{
	        .imageType = vk::ImageType::e2D,
	        .format = Configuration::get().get_precision() == 2 ? vk::Format::eR32Sfloat : vk::Format::eR16Sfloat,
	        .extent = {
	                .width = aligned_width / 2,
	                .height = aligned_height / 2,
	                .depth = 1,
	        },
	        .mipLevels = uint32_t(Configuration::get().get_precision() != 1 ? DecompositionLevels : WaveletFP16Levels),
	        .arrayLayers = NumFrequencyBandsPerLevel * NumComponents,
	        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst,
	};

	wavelet_img_high_res = image_allocation(
	        device,
	        info,
	        {
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        "wavelet_img_high_res");
	// FIXME: transition to general layout

	if (Configuration::get().get_precision() == 1)
	{
		// For the lowest level bands, we want to maintain precision as much as possible and bandwidth here is trivial.
		info.mipLevels = DecompositionLevels - info.mipLevels;
		info.format = vk::Format::eR32Sfloat;
		info.extent.width >>= WaveletFP16Levels;
		info.extent.height >>= WaveletFP16Levels;
		wavelet_img_low_res = image_allocation(
		        device,
		        info,
		        {
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "wavelet_img_low_res");
		// FIXME: transition to general layout
	}

	for (int level = 0; level < DecompositionLevels; level++)
	{
		vk::ImageViewCreateInfo view_info{
		        .viewType = vk::ImageViewType::e2D,
		        .subresourceRange = {
		                .aspectMask = vk::ImageAspectFlagBits::eColor,
		                .levelCount = 1,
		        },
		};
		image_allocation * image = nullptr;
		if (Configuration::get().get_precision() != 1 || level < WaveletFP16Levels)
		{
			view_info.subresourceRange.baseMipLevel = level;
			image = &wavelet_img_high_res;
			view_info.image = vk::Image(wavelet_img_high_res);
			view_info.format = wavelet_img_high_res.info().format;
		}
		else
		{
			view_info.subresourceRange.baseMipLevel = level - WaveletFP16Levels;
			image = &wavelet_img_low_res;
			view_info.image = vk::Image(wavelet_img_low_res);
			view_info.format = wavelet_img_low_res.info().format;
		}

		for (int component = 0; component < NumComponents; component++)
		{
			view_info.subresourceRange.baseArrayLayer = 4 * component;

			view_info.viewType = vk::ImageViewType::e2DArray;
			view_info.subresourceRange.layerCount = 4;
			component_layer_views[component][level] = device.createImageView(view_info);
			component_layer_views_info[component][level] = view_info;

			view_info.viewType = vk::ImageViewType::e2D;
			view_info.subresourceRange.layerCount = 1;
			component_ll_views[component][level] = device.createImageView(view_info);
			component_ll_dim[component][level].width = get_width(*image, view_info.subresourceRange.baseMipLevel);
			component_ll_dim[component][level].height = get_height(*image, view_info.subresourceRange.baseMipLevel);
		}
	}
}
void WaveletBuffers::begin_label(vk::raii::CommandBuffer & cmd, const char * label)
{
	if (has_debug_ext)
		cmd.beginDebugUtilsLabelEXT({.pLabelName = label});
}
void WaveletBuffers::end_label(vk::raii::CommandBuffer & cmd)
{
	if (has_debug_ext)
		cmd.endDebugUtilsLabelEXT();
}
} // namespace PyroWave
