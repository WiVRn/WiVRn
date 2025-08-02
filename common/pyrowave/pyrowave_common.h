// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#pragma once

#define PYROWAVE_PRECISION 1

#include <cassert>
#include <cstring>
#include <stdint.h>

#include "vk/allocation.h"

#include <vulkan/vulkan_raii.hpp>

namespace PyroWave
{

enum class ChromaSubsampling
{
	Chroma420,
	Chroma444
};

struct BitstreamPacket
{
	uint32_t offset_u32;
	uint32_t num_words;
};

struct BitstreamHeader
{
	uint16_t ballot;
	uint16_t payload_words : 12;
	uint16_t sequence : 3;
	uint16_t extended : 1;
	uint32_t quant_code : 8;
	uint32_t block_index : 24;
};

static_assert(sizeof(BitstreamHeader) == 8, "BitstreamHeader is not 8 bytes.");

enum
{
	BITSTREAM_EXTENDED_CODE_START_OF_FRAME = 0,
};

enum
{
	CHROMA_RESOLUTION_420 = 0,
	CHROMA_RESOLUTION_444 = 1
};

enum
{
	CHROMA_SITING_CENTER = 0,
	CHROMA_SITING_LEFT = 1
};

enum
{
	YCBCR_RANGE_FULL = 0,
	YCBCR_RANGE_LIMITED = 1
};

enum
{
	COLOR_PRIMARIES_BT709 = 0,
	COLOR_PRIMARIES_BT2020 = 1
};

enum
{
	YCBCR_TRANSFORM_BT709 = 0,
	YCBCR_TRANSFORM_BT2020 = 1
};

enum
{
	TRANSFER_FUNCTION_BT709 = 0,
	TRANSFER_FUNCTION_PQ = 1
};

static constexpr uint32_t SequenceCountMask = 0x7;

struct BitstreamSequenceHeader
{
	uint32_t width_minus_1 : 14;
	uint32_t height_minus_1 : 14;
	uint32_t sequence : 3;
	uint32_t extended : 1;
	uint32_t total_blocks : 24;
	uint32_t code : 2;
	uint32_t chroma_resolution : 1;
	uint32_t color_primaries : 1;
	uint32_t transfer_function : 1;
	uint32_t ycbcr_transform : 1;
	uint32_t ycbcr_range : 1;
	uint32_t chroma_siting : 1;
};

static_assert(sizeof(BitstreamSequenceHeader) == 8, "BitstreamSequenceHeader is not 8 bytes.");

struct QuantStats
{
	uint16_t square_error_fp16;
	uint16_t encode_cost_bits;
};

struct BlockStats
{
	uint32_t num_planes;
	QuantStats stats[15];
};
static_assert(sizeof(BlockStats) == 64, "BlockStats is not 64 bytes.");

struct BlockMeta
{
	uint32_t code_word;
	uint32_t offset;
};

static constexpr int DecompositionLevels = 5;
static constexpr int Alignment = 1 << DecompositionLevels;
// If the final decomposition band is too small, the mirroring will break since it starts double mirroring.
static constexpr int MinimumImageSize = 4 << DecompositionLevels;
static constexpr int NumComponents = 3;
static constexpr int NumFrequencyBandsPerLevel = 4;

static inline int align(int value, int align)
{
	return (value + align - 1) & ~(align - 1);
}

static constexpr int MaxScaleExp = 4;

static inline float decode_quant(uint8_t quant_code)
{
	// Custom FP formulation for numbers in (0, 2) range.
	int e = MaxScaleExp - (quant_code >> 3);
	int m = quant_code & 0x7;
	float inv_quant = (1.0f / (8.0f * 1024.0f * 1024.0f)) * float((8 + m) * (1 << (20 + e)));
	return inv_quant;
}

static inline uint8_t encode_quant(float decoder_q_scale)
{
	uint32_t v;
	memcpy(&v, &decoder_q_scale, sizeof(decoder_q_scale));

	int e = ((v >> 23) & 0xff) - 127 - MaxScaleExp;
	int m = (v >> 20) & 0x7;
	e = -e;
	assert(e >= 0 && e <= 20);
	return (e << 3) | m;
}

static inline auto get_width(const image_allocation & img, uint32_t lod = 0)
{
	return std::max<uint32_t>(1u, img.info().extent.width >> lod);
}
static inline auto get_height(const image_allocation & img, uint32_t lod = 0)
{
	return std::max<uint32_t>(1u, img.info().extent.height >> lod);
}

bool supports_subgroup_size_log2(
        vk::PhysicalDeviceVulkan13Properties const & prop13,
        bool subgroup_full_group,
        uint8_t subgroup_minimum_size_log2,
        uint8_t subgroup_maximum_size_log2,
        vk::ShaderStageFlagBits stage = vk::ShaderStageFlagBits::eCompute);

vk::raii::ShaderModule load_shader(vk::raii::Device & device, const std::string & name);

struct pipeline_subgroup_info
{
	vk::PipelineShaderStageRequiredSubgroupSizeCreateInfo info{};

	void set_subgroup_size(
	        vk::PhysicalDeviceVulkan13Properties const & prop13,
	        vk::ComputePipelineCreateInfo & pipeline_info,
	        uint8_t subgroup_minimum_size_log2,
	        uint8_t subgroup_maximum_size_log2)
	{
		const uint32_t min_subgroups = 1u << subgroup_minimum_size_log2;
		const uint32_t max_subgroups = 1u << subgroup_maximum_size_log2;
		if (min_subgroups <= prop13.minSubgroupSize and max_subgroups >= prop13.maxSubgroupSize)
		{
			pipeline_info.stage.flags |= vk::PipelineShaderStageCreateFlagBits::eAllowVaryingSubgroupSize;
		}
		else
		{
			info.pNext = const_cast<void *>(pipeline_info.stage.pNext);
			pipeline_info.stage.pNext = &info;
			// Pick a fixed subgroup size. Prefer smallest subgroup size.
			if (min_subgroups < prop13.minSubgroupSize)
				info.requiredSubgroupSize = prop13.minSubgroupSize;
			else
				info.requiredSubgroupSize = min_subgroups;
		}
	}
};

class Configuration
{
public:
	static Configuration & get();
	int get_precision() const;

private:
	Configuration();
	int precision;
};

struct WaveletBuffers
{
	WaveletBuffers(vk::raii::Device & device, int width, int height, ChromaSubsampling chroma);

	vk::raii::Device & device;
	image_allocation wavelet_img_low_res;
	image_allocation wavelet_img_high_res;
	vk::raii::Sampler mirror_repeat_sampler;
	vk::raii::Sampler border_sampler;
	vk::raii::ImageView component_layer_views[NumComponents][DecompositionLevels];
	vk::raii::ImageView component_ll_views[NumComponents][DecompositionLevels];
	vk::Extent2D component_ll_dim[NumComponents][DecompositionLevels];

	struct BlockInfo
	{
		int block_offset_8x8;
		int block_stride_8x8;
		int block_offset_32x32;
		int block_stride_32x32;
	};
	BlockInfo block_meta[NumComponents][DecompositionLevels][4] = {};

	struct BlockMapping
	{
		int block_offset_8x8;
		int block_stride_8x8;
		int block_width_8x8;
		int block_height_8x8;
	};
	std::vector<BlockMapping> block_32x32_to_8x8_mapping;

	int block_count_8x8 = 0;
	int block_count_32x32 = 0;

	int width = 0;
	int height = 0;
	uint32_t aligned_width = 0;
	uint32_t aligned_height = 0;

protected:
	bool has_debug_ext = false;
	void init_block_meta();
	void begin_label(vk::raii::CommandBuffer &, const char * label);
	void end_label(vk::raii::CommandBuffer &);
	ChromaSubsampling chroma;

private:
	void accumulate_block_mapping(int blocks_x_8x8, int blocks_y_8x8);
};
} // namespace PyroWave
