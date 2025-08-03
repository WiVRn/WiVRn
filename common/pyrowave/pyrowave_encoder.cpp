// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#include "pyrowave_encoder.h"
#include "pyrowave_common.h"
#include <algorithm>
#include <cmath>
#include <iostream>

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#define XSTR(s) STR(s)
#define STR(s) #s

inline uint32_t next_pow2(uint32_t v)
{
	v--;
	v |= v >> 16;
	v |= v >> 8;
	v |= v >> 4;
	v |= v >> 2;
	v |= v >> 1;
	return v + 1;
}

#define popcount32_(x) __builtin_popcount(x)
#define leading_zeroes_(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define trailing_zeroes_(x) ((x) == 0 ? 32 : __builtin_ctz(x))
static inline uint32_t leading_zeroes(uint32_t x)
{
	return leading_zeroes_(x);
}
static inline uint32_t popcount32(uint32_t x)
{
	return popcount32_(x);
}
inline uint32_t floor_log2(uint32_t v)
{
	return 31 - leading_zeroes(v);
}
static inline uint32_t trailing_zeroes(uint32_t x)
{
	return trailing_zeroes_(x);
}

template <typename T>
inline void for_each_bit(uint32_t value, const T & func)
{
	while (value)
	{
		uint32_t bit = trailing_zeroes(value);
		func(bit);
		value &= ~(1u << bit);
	}
}

namespace PyroWave
{

static constexpr int BlockSpaceSubdivision = 16;
static constexpr int NumRDOBuckets = 128;
static constexpr int RDOBucketOffset = 64;

static int compute_block_count_per_subdivision(int num_blocks)
{
	int per_subdivision = align(num_blocks, BlockSpaceSubdivision) / BlockSpaceSubdivision;
	per_subdivision = int(next_pow2(per_subdivision));
	return per_subdivision;
}

struct QuantizerPushData
{
	glm::ivec2 resolution;
	glm::ivec2 resolution_8x8_blocks;
	glm::vec2 inv_resolution;
	float input_layer;
	float quant_resolution;
	int32_t block_offset;
	int32_t block_stride;
	float rdo_distortion_scale;
};

struct BlockPackingPushData
{
	glm::ivec2 resolution;
	glm::ivec2 resolution_32x32_blocks;
	glm::ivec2 resolution_8x8_blocks;
	uint32_t quant_resolution_code;
	uint32_t sequence_count;
	uint32_t block_offset_32x32;
	uint32_t block_stride_32x32;
	uint32_t block_offset_8x8;
	uint32_t block_stride_8x8;
};

struct AnalyzeRateControlPushData
{
	glm::ivec2 resolution;
	glm::ivec2 resolution_8x8_blocks;
	int32_t block_offset_8x8;
	int32_t block_stride_8x8;
	int32_t block_offset_32x32;
	int32_t block_stride_32x32;
	uint32_t total_wg_count;
	uint32_t num_blocks_aligned;
	uint32_t block_index_shamt;
};
struct DwtPushData
{
	glm::uvec2 resolution;
	glm::vec2 inv_resolution;
	glm::uvec2 aligned_resolution;
};

struct RDOperation
{
	int32_t quant;
	uint16_t block_offset;
	uint16_t block_saving;
};

static vk::raii::DescriptorPool make_descriptor_pool(vk::raii::Device & device)
{
	std::array pool_sizes{
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageBuffer,
	                .descriptorCount =
	                        6                                         // block_packing
	                        + 2                                       // resolve_rdo
	                        + 2                                       // analyze_rdo / analyze_rdo_finalize
	                        + 3 * NumComponents * DecompositionLevels // quant
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount =
	                        2 * NumComponents * DecompositionLevels // quant + dwt
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageImage,
	                .descriptorCount =
	                        NumComponents * DecompositionLevels // dwt
	        },
	};
	return {device,
	        vk::DescriptorPoolCreateInfo{
	                .maxSets =
	                        3                                         // block_packing, resolve_rdo, (analyze_rdo / analyze_rdo_finalize)
	                        + 2 * NumComponents * DecompositionLevels // quant + dwt
	                ,
	                .poolSizeCount = pool_sizes.size(),
	                .pPoolSizes = pool_sizes.data(),
	        }};
}

Encoder::Encoder(vk::raii::PhysicalDevice & phys_dev, vk::raii::Device & device, int width, int height, ChromaSubsampling chroma) :
        WaveletBuffers(device, width, height, chroma),
        ds_pool(make_descriptor_pool(device))
{
	auto [prop, prop11, prop13] = phys_dev.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan11Properties, vk::PhysicalDeviceVulkan13Properties>();
	auto ops = prop11.subgroupSupportedOperations;
	constexpr auto required_features =
	        vk::SubgroupFeatureFlagBits::eArithmetic |
	        vk::SubgroupFeatureFlagBits::eShuffle |
	        vk::SubgroupFeatureFlagBits::eShuffleRelative |
	        vk::SubgroupFeatureFlagBits::eVote |
	        vk::SubgroupFeatureFlagBits::eQuad |
	        vk::SubgroupFeatureFlagBits::eBallot |
	        vk::SubgroupFeatureFlagBits::eClustered |
	        vk::SubgroupFeatureFlagBits::eBasic;

	if ((ops & required_features) != required_features)
	{
		throw std::runtime_error("There are missing subgroup features. Device supports " + vk::to_string(ops) + ", but requires " + vk::to_string(required_features) + ".");
	}

	auto [feat, feat12, feat13] = phys_dev.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();

	if (not feat12.subgroupBroadcastDynamicId)
		throw std::runtime_error("Missing subgroupBroadcastDynamicId feature");

	if (not feat12.storageBuffer8BitAccess)
		throw std::runtime_error("Missing storageBuffer8BitAccess feature");

	if (not feat12.shaderFloat16)
		throw std::runtime_error("Missing shaderFloat16 feature");

	if (not feat.features.shaderInt16)
		throw std::runtime_error("Missing shaderInt16 feature");

	if (not feat13.computeFullSubgroups)
		throw std::runtime_error("Missing computeFullSubgroups feature");

	// This should cover any HW I care about.
	if (!supports_subgroup_size_log2(prop13, true, 4, 4) &&
	    !supports_subgroup_size_log2(prop13, true, 5, 5) &&
	    !supports_subgroup_size_log2(prop13, true, 6, 6))
		throw std::runtime_error("Device does not have the required subgroup properties");

	// init block meta
	{
		init_block_meta();
		vk::BufferCreateInfo info{
		        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst
		        //| vk::BufferUsageFlagBits::eTransferSrc // DEBUG
		        ,
		};

		info.size = block_count_8x8 * sizeof(BlockStats);
		block_stat_buffer = buffer_allocation(device, info, {.usage = VMA_MEMORY_USAGE_AUTO}, "block_stat_buffer");
		// device->set_name(*block_stat_buffer, "block-stat-buffer");

		info.size = block_count_8x8 * sizeof(BlockMeta);
		meta_buffer = buffer_allocation(device, info, {.usage = VMA_MEMORY_USAGE_AUTO}, "meta_buffer");
		// device->set_name(*meta_buffer, "meta-buffer");

		// Worst case estimate.
		info.size = aligned_width * aligned_height * 2;
		payload_data = buffer_allocation(device, info, {.usage = VMA_MEMORY_USAGE_AUTO}, "payload_data");
		// device->set_name(*payload_data, "payload-data");

		info.size = block_count_32x32 * sizeof(uint32_t);
		quant_buffer = buffer_allocation(device, info, {.usage = VMA_MEMORY_USAGE_AUTO}, "quant_buffer");
		// device->set_name(*quant_buffer, "quant-buffer");

		info.size = RDOBucketOffset;
		info.size += NumRDOBuckets * BlockSpaceSubdivision * sizeof(uint32_t);
		info.size += NumRDOBuckets * compute_block_count_per_subdivision(block_count_32x32) *
		             BlockSpaceSubdivision * sizeof(RDOperation);
		bucket_buffer = buffer_allocation(device, info, {.usage = VMA_MEMORY_USAGE_AUTO}, "bucket_buffer");
		// device->set_name(*bucket_buffer, "bucket-buffer");
	}

	// block_packing pipeline
	{
		std::array bindings{
		        vk::DescriptorSetLayoutBinding{
		                .binding = 0,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 1,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 2,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 3,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 4,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 5,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		};
		block_packing_.ds_layout = device.createDescriptorSetLayout({
		        .bindingCount = bindings.size(),
		        .pBindings = bindings.data(),
		});

		block_packing_.ds = device.allocateDescriptorSets({
		        .descriptorPool = *ds_pool,
		        .descriptorSetCount = 1,
		        .pSetLayouts = &*block_packing_.ds_layout,
		})[0]
		                            .release();

		vk::PushConstantRange pc{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .size = sizeof(BlockPackingPushData),
		};

		block_packing_.layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*block_packing_.ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &pc,
		});
		const uint32_t min_subgroups = 1u << 4;
		const uint32_t max_subgroups = 1u << 6;

		vk::PipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup_size{};
		auto shader = load_shader(device, "block_packing");
		vk::ComputePipelineCreateInfo info{
		        .stage = {
		                .flags = vk::PipelineShaderStageCreateFlagBits::eRequireFullSubgroups,
		                .stage = vk::ShaderStageFlagBits::eCompute,
		                .module = *shader,
		                .pName = "main",
		        },
		        .layout = *block_packing_.layout,
		};
		pipeline_subgroup_info psi;
		psi.set_subgroup_size(prop13, info, 4, 6);
		block_packing_.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		std::array buffer_info{
		        vk::DescriptorBufferInfo{
		                .buffer = meta_buffer,
		                .range = vk::WholeSize,
		        },
		        vk::DescriptorBufferInfo{
		                .buffer = payload_data,
		                .range = vk::WholeSize,
		        },
		        vk::DescriptorBufferInfo{
		                .buffer = block_stat_buffer,
		                .range = vk::WholeSize,
		        },
		        vk::DescriptorBufferInfo{
		                .buffer = quant_buffer,
		                .range = vk::WholeSize,
		        },
		};

		std::array descriptor_writes{
		        vk::WriteDescriptorSet{
		                .dstSet = block_packing_.ds,
		                .dstBinding = 2,
		                .descriptorCount = buffer_info.size(),
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .pBufferInfo = buffer_info.data(),
		        },
		};
		device.updateDescriptorSets(descriptor_writes, {});
	}

	// resolve rdo pipeline
	{
		std::array bindings{
		        vk::DescriptorSetLayoutBinding{
		                .binding = 0,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 1,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		};
		resolve_rdo_.ds_layout = device.createDescriptorSetLayout({
		        .bindingCount = bindings.size(),
		        .pBindings = bindings.data(),
		});

		resolve_rdo_.ds = device.allocateDescriptorSets({
		        .descriptorPool = *ds_pool,
		        .descriptorSetCount = 1,
		        .pSetLayouts = &*resolve_rdo_.ds_layout,
		})[0]
		                          .release();

		vk::PushConstantRange pc{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .size = sizeof(uint32_t) * 2,
		};

		resolve_rdo_.layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*resolve_rdo_.ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &pc,
		});
		uint32_t block_space_subdivision;
		vk::SpecializationMapEntry sp_entry{
		        .constantID = 0,
		        .size = sizeof(block_space_subdivision),
		};
		vk::SpecializationInfo sp{
		        .mapEntryCount = 1,
		        .pMapEntries = &sp_entry,
		        .dataSize = sizeof(block_space_subdivision),
		        .pData = &block_space_subdivision,
		};
		auto shader = load_shader(device, "resolve_rate_control");
		vk::ComputePipelineCreateInfo info{
		        .stage = {
		                .flags = vk::PipelineShaderStageCreateFlagBits::eRequireFullSubgroups,
		                .stage = vk::ShaderStageFlagBits::eCompute,
		                .module = *shader,
		                .pName = "main",
		                .pSpecializationInfo = &sp,
		        },
		        .layout = *resolve_rdo_.layout,
		};
		pipeline_subgroup_info psi;
		if (supports_subgroup_size_log2(prop13, true, 6, 6))
		{
			block_space_subdivision = 64;
			psi.set_subgroup_size(prop13, info, 6, 6);
		}
		else if (supports_subgroup_size_log2(prop13, true, 4, 4))
		{
			block_space_subdivision = 16;
			psi.set_subgroup_size(prop13, info, 4, 4);
		}
		else if (supports_subgroup_size_log2(prop13, true, 5, 5))
		{
			block_space_subdivision = 32;
			psi.set_subgroup_size(prop13, info, 5, 5);
		}
		else
		{
			assert(false);
		}
		resolve_rdo_.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		std::array buffer_info{
		        vk::DescriptorBufferInfo{
		                .buffer = bucket_buffer,
		                .range = vk::WholeSize,
		        },
		        vk::DescriptorBufferInfo{
		                .buffer = quant_buffer,
		                .range = vk::WholeSize,
		        },
		};

		std::array descriptor_writes{
		        vk::WriteDescriptorSet{
		                .dstSet = resolve_rdo_.ds,
		                .dstBinding = 0,
		                .descriptorCount = buffer_info.size(),
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .pBufferInfo = buffer_info.data(),
		        },
		};
		device.updateDescriptorSets(descriptor_writes, {});
	}

	// analyze rdo pipeline
	{
		std::array bindings{
		        vk::DescriptorSetLayoutBinding{
		                .binding = 0,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 1,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		};
		analyze_rdo_.ds_layout = device.createDescriptorSetLayout({
		        .bindingCount = bindings.size(),
		        .pBindings = bindings.data(),
		});

		analyze_rdo_.ds = device.allocateDescriptorSets({
		        .descriptorPool = *ds_pool,
		        .descriptorSetCount = 1,
		        .pSetLayouts = &*analyze_rdo_.ds_layout,
		})[0]
		                          .release();

		vk::PushConstantRange pc{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .size = sizeof(AnalyzeRateControlPushData),
		};

		analyze_rdo_.layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*analyze_rdo_.ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &pc,
		});
		auto shader = load_shader(device, "analyze_rate_control");
		vk::ComputePipelineCreateInfo info{
		        .stage = {
		                .flags = vk::PipelineShaderStageCreateFlagBits::eRequireFullSubgroups,
		                .stage = vk::ShaderStageFlagBits::eCompute,
		                .module = *shader,
		                .pName = "main",
		        },
		        .layout = *analyze_rdo_.layout,
		};
		pipeline_subgroup_info psi;
		if (supports_subgroup_size_log2(prop13, true, 4, 6))
		{
			psi.set_subgroup_size(prop13, info, 4, 6);
		}
		else
		{
			assert(false);
		}
		analyze_rdo_.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		std::array buffer_info{
		        vk::DescriptorBufferInfo{
		                .buffer = bucket_buffer,
		                .range = vk::WholeSize,
		        },
		        vk::DescriptorBufferInfo{
		                .buffer = block_stat_buffer,
		                .range = vk::WholeSize,
		        },
		};

		std::array descriptor_writes{
		        vk::WriteDescriptorSet{
		                .dstSet = analyze_rdo_.ds,
		                .dstBinding = 0,
		                .descriptorCount = buffer_info.size(),
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .pBufferInfo = buffer_info.data(),
		        },
		};
		device.updateDescriptorSets(descriptor_writes, {});
	}

	// analyze rdo finalize pipeline
	{
		vk::PushConstantRange pc{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .size = sizeof(AnalyzeRateControlPushData),
		};

		analyze_rdo_finalize.layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*analyze_rdo_.ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &pc,
		});
		auto shader = load_shader(device, "analyze_rate_control_finalize");
		vk::ComputePipelineCreateInfo info{
		        .stage = {
		                .stage = vk::ShaderStageFlagBits::eCompute,
		                .module = *shader,
		                .pName = "main",
		        },
		        .layout = *analyze_rdo_finalize.layout,
		};
		analyze_rdo_finalize.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);
	}

	// quant pipeline
	{
		std::array bindings{
		        vk::DescriptorSetLayoutBinding{
		                .binding = 0,
		                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 1,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 2,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 3,
		                .descriptorType = vk::DescriptorType::eStorageBuffer,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		};
		quant_.ds_layout = device.createDescriptorSetLayout({
		        .bindingCount = bindings.size(),
		        .pBindings = bindings.data(),
		});

		vk::PushConstantRange pc{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .size = sizeof(QuantizerPushData),
		};

		quant_.layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*quant_.ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &pc,
		});
		auto shader = load_shader(device, "wavelet_quant");
		vk::ComputePipelineCreateInfo info{
		        .stage = {
		                .flags = vk::PipelineShaderStageCreateFlagBits::eRequireFullSubgroups,
		                .stage = vk::ShaderStageFlagBits::eCompute,
		                .module = *shader,
		                .pName = "main",
		        },
		        .layout = *quant_.layout,
		};
		assert(supports_subgroup_size_log2(prop13, true, 3, 7));
		pipeline_subgroup_info psi;
		psi.set_subgroup_size(prop13, info, 3, 7);
		quant_.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		for (int level = 0; level < DecompositionLevels; level++)
		{
			for (int component = 0; component < NumComponents; component++)
			{
				quant_ds[component][level] = device.allocateDescriptorSets({
				        .descriptorPool = *ds_pool,
				        .descriptorSetCount = 1,
				        .pSetLayouts = &*quant_.ds_layout,
				})[0]
				                                     .release();
				std::array image_info{
				        vk::DescriptorImageInfo{
				                .sampler = *border_sampler,
				                .imageView = *component_layer_views[component][level],
				                .imageLayout = vk::ImageLayout::eGeneral,
				        },
				};
				std::array buffer_info{
				        vk::DescriptorBufferInfo{
				                .buffer = meta_buffer,
				                .range = vk::WholeSize,
				        },
				        vk::DescriptorBufferInfo{
				                .buffer = block_stat_buffer,
				                .range = vk::WholeSize,
				        },
				        vk::DescriptorBufferInfo{
				                .buffer = payload_data,
				                .range = vk::WholeSize,
				        },
				};

				std::array descriptor_writes{
				        vk::WriteDescriptorSet{
				                .dstSet = quant_ds[component][level],
				                .dstBinding = 0,
				                .descriptorCount = image_info.size(),
				                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				                .pImageInfo = image_info.data(),
				        },
				        vk::WriteDescriptorSet{
				                .dstSet = quant_ds[component][level],
				                .dstBinding = 1,
				                .descriptorCount = buffer_info.size(),
				                .descriptorType = vk::DescriptorType::eStorageBuffer,
				                .pBufferInfo = buffer_info.data(),
				        },
				};
				device.updateDescriptorSets(descriptor_writes, {});
			}
		}
	}

	// dwt pipeline
	{
		std::array bindings{
		        vk::DescriptorSetLayoutBinding{
		                .binding = 0,
		                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		        vk::DescriptorSetLayoutBinding{
		                .binding = 1,
		                .descriptorType = vk::DescriptorType::eStorageImage,
		                .descriptorCount = 1,
		                .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        },
		};
		dwt_.ds_layout = device.createDescriptorSetLayout({
		        .bindingCount = bindings.size(),
		        .pBindings = bindings.data(),
		});

		vk::PushConstantRange pc{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .size = sizeof(DwtPushData),
		};

		dwt_.layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*dwt_.ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &pc,
		});
		VkBool32 dc_shift = false;
		vk::SpecializationMapEntry sp_entry{
		        .constantID = 0,
		        .size = sizeof(dc_shift),
		};
		vk::SpecializationInfo sp{
		        .mapEntryCount = 1,
		        .pMapEntries = &sp_entry,
		        .dataSize = sizeof(dc_shift),
		        .pData = &dc_shift,
		};
		auto shader = load_shader(device, "dwt_" XSTR(PYROWAVE_PRECISION));
		vk::ComputePipelineCreateInfo info{
		        .stage = {
		                .flags = vk::PipelineShaderStageCreateFlagBits::eRequireFullSubgroups,
		                .stage = vk::ShaderStageFlagBits::eCompute,
		                .module = *shader,
		                .pName = "main",
		                .pSpecializationInfo = &sp,
		        },
		        .layout = *dwt_.layout,
		};
		pipeline_subgroup_info psi;
		// Only need simple 2-lane swaps.
		psi.set_subgroup_size(prop13, info, 2, 7);
		dwt_.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		dc_shift = true;
		dwt_dcshift = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		for (int level = 0; level < DecompositionLevels; level++)
		{
			for (int component = 0; component < NumComponents; component++)
			{
				dwt_ds[component][level] = device.allocateDescriptorSets({
				        .descriptorPool = *ds_pool,
				        .descriptorSetCount = 1,
				        .pSetLayouts = &*dwt_.ds_layout,
				})[0]
				                                   .release();

				vk::DescriptorImageInfo storage{
				        .imageView = *component_layer_views[component][level],
				        .imageLayout = vk::ImageLayout::eGeneral,
				};
				vk::WriteDescriptorSet descriptor_write{
				        .dstSet = dwt_ds[component][level],
				        .dstBinding = 1,
				        .descriptorCount = 1,
				        .descriptorType = vk::DescriptorType::eStorageImage,
				        .pImageInfo = &storage,
				};
				device.updateDescriptorSets(descriptor_write, {});

				// Updated with input image
				if (level == 0)
					continue;
				if (level == 1 and component > 0 and chroma == ChromaSubsampling::Chroma420)
					continue;

				vk::DescriptorImageInfo sampled{
				        .sampler = *mirror_repeat_sampler,
				        .imageView = *component_ll_views[component][level - 1],
				        .imageLayout = vk::ImageLayout::eGeneral,
				};

				descriptor_write = {
				        .dstSet = dwt_ds[component][level],
				        .dstBinding = 0,
				        .descriptorCount = 1,
				        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				        .pImageInfo = &sampled,
				};

				device.updateDescriptorSets(descriptor_write, {});
			}
		}
	}

	/*
	{
	        vk::BufferCreateInfo info{
	                .size = vk::DeviceSize(width * height * 4),
	                .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
	        };
	        debug_buffer = buffer_allocation(device, info, {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT, .usage = VMA_MEMORY_USAGE_AUTO}, "block_stat_buffer");
	}
	*/
}

float Encoder::get_quant_rdo_distortion_scale(int level, int component, int band) const
{
	// From my Linelet master thesis. Copy paste 11 years later, ah yes :D
	float horiz_midpoint = (band & 1) ? 0.75f : 0.25f;
	float vert_midpoint = (band & 2) ? 0.75f : 0.25f;

	// Normal PC monitors.
	constexpr float dpi = 96.0f;
	// Compromise between couch gaming and desktop.
	constexpr float viewing_distance = 1.0f;
	constexpr float cpd_nyquist = 0.34f * viewing_distance * dpi;

	float cpd = std::sqrt(horiz_midpoint * horiz_midpoint + vert_midpoint * vert_midpoint) *
	            cpd_nyquist * std::exp2(-float(level));

	// Don't allow a situation where we're quantizing LL band hard.
	cpd = std::max(cpd, 8.0f);

	float csf = 2.6f * (0.0192f + 0.114f * cpd) * std::exp(-std::pow(0.114f * cpd, 1.1f));

	// Heavily discount chroma quality.
	if (component != 0 && level != DecompositionLevels - 1)
	{
		// Consider chroma a little more important if we're not subsampling.
		if (chroma == ChromaSubsampling::Chroma420)
			csf *= 0.6f;
	}

	// Due to filtering, distortion in lower bands will result in more noise power.
	// By scaling the distortion by this factor, we ensure uniform results.
	float resolution = get_noise_power_normalized_quant_resolution(level, component, band);
	float weighted_resolution = csf * resolution;

	// The distortion is scaled in terms of power, not amplitude.
	return weighted_resolution * weighted_resolution;
}

float Encoder::get_quant_resolution(int level, int component, int band) const
{
	// FP16 range is limited, and this is more than a good enough initial estimate.
	return std::min<float>(
	        Configuration::get().get_precision() >= 1 ? 4096.0f : 512.0f,
	        get_noise_power_normalized_quant_resolution(level, component, band));
}

float Encoder::get_noise_power_normalized_quant_resolution(int level, int component, int band) const
{
	// The initial quantization resolution aims for a flat spectrum with noise power normalization.
	// The low-pass gain for CDF 9/7 is 6 dB (1 bit). Every decomposition level subtracts 6 dB.

	// Maybe make this based on the max rate to have a decent initial estimate.
	int bits = Configuration::get().get_precision() >= 1 ? 8 : 6;

	if (band == 0)
		bits += 2;
	else if (band < 3)
		bits += 1;

	bits += level;

	// Chroma starts at level 1, subtract one bit.
	if (component != 0)
		bits--;

	return float(1 << bits);
}

bool Encoder::block_packing(vk::raii::CommandBuffer & cmd, const BitstreamBuffers & buffers, float quant_scale)
{
	// debug utils
	begin_label(cmd, "DWT block packing");

	// timing
	// auto start_packing = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	std::array ds_buffers{
	        vk::DescriptorBufferInfo{
	                .buffer = buffers.bitstream.buffer,
	                .offset = buffers.bitstream.offset,
	                .range = buffers.bitstream.size,
	        },
	        vk::DescriptorBufferInfo{
	                .buffer = buffers.meta.buffer,
	                .offset = buffers.meta.offset,
	                .range = buffers.meta.size,
	        },
	};
	vk::WriteDescriptorSet descriptor_write{
	        .dstSet = block_packing_.ds,
	        .dstBinding = 0,
	        .descriptorCount = ds_buffers.size(),
	        .descriptorType = vk::DescriptorType::eStorageBuffer,
	        .pBufferInfo = ds_buffers.data(),
	};
	device.updateDescriptorSets(descriptor_write, {});
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *block_packing_.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *block_packing_.layout, 0, block_packing_.ds, {});

	for (int level = 0; level < DecompositionLevels; level++)
	{
		auto level_width = get_width(wavelet_img_high_res, level);
		auto level_height = get_height(wavelet_img_high_res, level);

		for (int component = 0; component < NumComponents; component++)
		{
			// Ignore top-level CbCr when doing 420 subsampling.
			if (level == 0 && component != 0 && chroma == ChromaSubsampling::Chroma420)
				continue;

			begin_label(cmd, std::format("level {}, component {}", level, component).c_str());

			for (int band = (level == DecompositionLevels - 1 ? 0 : 1); band < 4; band++)
			{
				BlockPackingPushData packing_push = {};
				packing_push.resolution = glm::ivec2(level_width, level_height);
				packing_push.resolution_32x32_blocks = glm::ivec2((level_width + 31) / 32, (level_height + 31) / 32);
				packing_push.resolution_8x8_blocks = glm::ivec2((level_width + 7) / 8, (level_height + 7) / 8);

				auto quant_res = quant_scale < 0.0f ? get_quant_resolution(level, component, band) : quant_scale;
				packing_push.quant_resolution_code = encode_quant(1.0f / quant_res);
				packing_push.sequence_count = sequence_count;

				auto & meta = block_meta[component][level][band];

				packing_push.block_offset_32x32 = meta.block_offset_32x32;
				packing_push.block_stride_32x32 = meta.block_stride_32x32;
				packing_push.block_offset_8x8 = meta.block_offset_8x8;
				packing_push.block_stride_8x8 = meta.block_stride_8x8;
				cmd.pushConstants<BlockPackingPushData>(*block_packing_.layout, vk::ShaderStageFlagBits::eCompute, 0, packing_push);

				cmd.dispatch((packing_push.resolution_32x32_blocks.x + 1) / 2,
				             (packing_push.resolution_32x32_blocks.y + 1) / 2,
				             1);
			}

			end_label(cmd);
		}
	}

	// auto end_packing = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	vk::MemoryBarrier barrier{
	        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
	        .dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eTransferRead,
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eAllCommands, // FIXME: clear and copy bits
	        {},
	        barrier,
	        {},
	        {});
	// cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT);

	// device->register_time_interval("GPU", std::move(start_packing), std::move(end_packing), "Packing");
	end_label(cmd);

	return true;
}

bool Encoder::resolve_rdo(vk::raii::CommandBuffer & cmd, size_t target_payload_size)
{
	begin_label(cmd, "DWT resolve");

	// auto start_resolve = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	if (target_payload_size >= sizeof(BitstreamSequenceHeader))
		target_payload_size -= sizeof(BitstreamSequenceHeader);

	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *resolve_rdo_.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *resolve_rdo_.layout, 0, resolve_rdo_.ds, {});

	struct
	{
		uint32_t target_payload_size;
		uint32_t num_blocks_per_subdivision;
	} push = {};

	push.target_payload_size = target_payload_size / sizeof(uint32_t);
	push.num_blocks_per_subdivision = compute_block_count_per_subdivision(block_count_32x32);
	cmd.pushConstants<decltype(push)>(*resolve_rdo_.layout, vk::ShaderStageFlagBits::eCompute, 0, push);
	cmd.dispatch(NumRDOBuckets * BlockSpaceSubdivision, 1, 1);

	vk::MemoryBarrier barrier{
	        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
	        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        barrier,
	        {},
	        {});
	// cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
	end_label(cmd);

	// auto end_resolve = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	// device->register_time_interval("GPU", std::move(start_resolve), std::move(end_resolve), "Resolve");
	return true;
}

bool Encoder::analyze_rdo(vk::raii::CommandBuffer & cmd)
{
	// auto start_analyze = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	begin_label(cmd, "DWT analyze");
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *analyze_rdo_.pipeline);
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *analyze_rdo_.layout, 0, analyze_rdo_.ds, {});

	// Quantize
	for (int level = 0; level < DecompositionLevels; level++)
	{
		for (int component = 0; component < NumComponents; component++)
		{
			// Ignore top-level CbCr when doing 420 subsampling.
			if (level == 0 && component != 0 && chroma == ChromaSubsampling::Chroma420)
				continue;

			AnalyzeRateControlPushData push = {};

			begin_label(cmd, std::format("level {}, component {}", level, component).c_str());

			for (int band = (level == DecompositionLevels - 1 ? 0 : 1); band < 4; band++)
			{
				auto level_width = get_width(wavelet_img_high_res, level);
				auto level_height = get_height(wavelet_img_high_res, level);

				push.resolution.x = level_width;
				push.resolution.y = level_height;
				push.resolution_8x8_blocks.x = (level_width + 7) / 8;
				push.resolution_8x8_blocks.y = (level_height + 7) / 8;
				push.block_offset_8x8 = block_meta[component][level][band].block_offset_8x8;
				push.block_stride_8x8 = block_meta[component][level][band].block_stride_8x8;
				push.block_offset_32x32 = block_meta[component][level][band].block_offset_32x32;
				push.block_stride_32x32 = block_meta[component][level][band].block_stride_32x32;
				push.total_wg_count = block_count_32x32;
				push.num_blocks_aligned = compute_block_count_per_subdivision(block_count_32x32) * BlockSpaceSubdivision;
				push.block_index_shamt = floor_log2(compute_block_count_per_subdivision(block_count_32x32));

				cmd.pushConstants<AnalyzeRateControlPushData>(*analyze_rdo_.layout, vk::ShaderStageFlagBits::eCompute, 0, push);

				cmd.dispatch((level_width + 31) / 32, (level_height + 31) / 32, 1);
			}

			end_label(cmd);
		}
	}

	vk::MemoryBarrier barrier{
	        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
	        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eComputeShader, // FIXME: clear and copy bits
	        {},
	        barrier,
	        {},
	        {});
	// cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *analyze_rdo_finalize.pipeline);
	cmd.dispatch(1, 1, 1);

	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eComputeShader, // FIXME: clear and copy bits
	        {},
	        barrier,
	        {},
	        {});
	// cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

	end_label(cmd);
	// auto end_analyze = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	// device->register_time_interval("GPU", std::move(start_analyze), std::move(end_analyze), "Analyze");
	return true;
}

bool Encoder::quant(vk::raii::CommandBuffer & cmd, float quant_scale)
{
	// auto start_quant = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	begin_label(cmd, "DWT quantize");
	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *quant_.pipeline);

	// Quantize
	for (int level = 0; level < DecompositionLevels; level++)
	{
		for (int component = 0; component < NumComponents; component++)
		{
			// Ignore top-level CbCr when doing 420 subsampling.
			if (level == 0 && component != 0 && chroma == ChromaSubsampling::Chroma420)
				continue;

			QuantizerPushData push = {};

			begin_label(cmd, std::format("DWT quant level {}, component {}", level, component).c_str());

			for (int band = (level == DecompositionLevels - 1 ? 0 : 1); band < 4; band++)
			{
				float quant_res = quant_scale < 0.0f ? get_quant_resolution(level, component, band) : quant_scale;

				push.resolution.x = get_width(wavelet_img_high_res, level);
				push.resolution.y = get_height(wavelet_img_high_res, level);
				push.resolution_8x8_blocks.x = (push.resolution.x + 7) / 8;
				push.resolution_8x8_blocks.y = (push.resolution.y + 7) / 8;
				push.inv_resolution.x = 1.0f / float(push.resolution.x);
				push.inv_resolution.y = 1.0f / float(push.resolution.y);
				push.input_layer = float(band);
				push.quant_resolution = 1.0f / decode_quant(encode_quant(1.0f / quant_res));
				push.rdo_distortion_scale = get_quant_rdo_distortion_scale(level, component, band) * (1.0f / 256.0f);

				int blocks_x = (push.resolution.x + 31) / 32;
				int blocks_y = (push.resolution.y + 31) / 32;

				push.block_offset = block_meta[component][level][band].block_offset_8x8;
				push.block_stride = block_meta[component][level][band].block_stride_8x8;

				cmd.pushConstants<QuantizerPushData>(*quant_.layout, vk::ShaderStageFlagBits::eCompute, 0, push);
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *quant_.layout, 0, quant_ds[component][level], {});

				cmd.dispatch(blocks_x, blocks_y, 1);
			}

			end_label(cmd);
		}
	}

	end_label(cmd);
	// cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
	vk::MemoryBarrier barrier{
	        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
	        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        barrier,
	        {},
	        {});

	// auto end_quant = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	// device->register_time_interval("GPU", std::move(start_quant), std::move(end_quant), "Quant");
	return true;
}

bool Encoder::dwt(vk::raii::CommandBuffer & cmd, const ViewBuffers & views)
{
	DwtPushData push = {};

	// Forward transforms.

	// auto start_dwt = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	for (int output_level = 0; output_level < DecompositionLevels; output_level++)
	{
		if (output_level > 0)
		{
			push.resolution = glm::uvec2(component_ll_dim[0][output_level - 1].width,
			                             component_ll_dim[0][output_level - 1].height);
			push.aligned_resolution = push.resolution;
		}
		else
		{
			// FIXME: correct dimensions? original code uses views information
			push.resolution = glm::uvec2(width, height);
			push.aligned_resolution.x = aligned_width;
			push.aligned_resolution.y = aligned_height;
		}

		push.inv_resolution.x = 1.0f / float(push.resolution.x);
		push.inv_resolution.y = 1.0f / float(push.resolution.y);
		cmd.pushConstants<DwtPushData>(*dwt_.layout, vk::ShaderStageFlagBits::eCompute, 0, push);

		if (output_level == 0)
		{
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *dwt_dcshift);

			if (chroma == ChromaSubsampling::Chroma444)
			{
				for (int c = 0; c < NumComponents; c++)
				{
					vk::DescriptorImageInfo sampled{
					        .sampler = *mirror_repeat_sampler,
					        .imageView = views[c],
					        .imageLayout = vk::ImageLayout::eGeneral,
					};
					vk::WriteDescriptorSet descriptor_write{
					        .dstSet = dwt_ds[c][0],
					        .dstBinding = 0,
					        .descriptorCount = 1,
					        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
					        .pImageInfo = &sampled,
					};
					device.updateDescriptorSets(descriptor_write, {});
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *dwt_.layout, 0, dwt_ds[c][0], {});
					begin_label(cmd, std::format("DWT level 0, component {}", c).c_str());
					cmd.dispatch((push.aligned_resolution.x + 31) / 32, (push.aligned_resolution.y + 31) / 32, 1);
					end_label(cmd);
				}
			}
			else
			{
				begin_label(cmd, "DWT level 0 Y");
				vk::DescriptorImageInfo sampled{
				        .sampler = *mirror_repeat_sampler,
				        .imageView = views[0],
				        .imageLayout = vk::ImageLayout::eGeneral,
				};
				vk::WriteDescriptorSet descriptor_write{
				        .dstSet = dwt_ds[0][0],
				        .dstBinding = 0,
				        .descriptorCount = 1,
				        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				        .pImageInfo = &sampled,
				};
				device.updateDescriptorSets(descriptor_write, {});
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *dwt_.layout, 0, dwt_ds[0][0], {});
				cmd.dispatch((push.aligned_resolution.x + 31) / 32, (push.aligned_resolution.y + 31) / 32, 1);
				end_label(cmd);
			}
		}
		else
		{
			for (int c = 0; c < NumComponents; c++)
			{
				begin_label(cmd, std::format("DWT level {}, component {}", output_level, c).c_str());
				if (chroma == ChromaSubsampling::Chroma420 && c != 0 && output_level == 1)
				{
					vk::DescriptorImageInfo sampled{
					        .sampler = *mirror_repeat_sampler,
					        .imageView = views[c],
					        .imageLayout = vk::ImageLayout::eGeneral,
					};
					vk::WriteDescriptorSet descriptor_write{
					        .dstSet = dwt_ds[c][1],
					        .dstBinding = 0,
					        .descriptorCount = 1,
					        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
					        .pImageInfo = &sampled,
					};
					device.updateDescriptorSets(descriptor_write, {});
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *dwt_.layout, 0, dwt_ds[c][output_level], {});
					cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *dwt_dcshift);

					// FIXME: correct dimensions? original code uses views information
					push.resolution = glm::uvec2(width / 2, height / 2);

					push.aligned_resolution.x = aligned_width >> output_level;
					push.aligned_resolution.y = aligned_height >> output_level;
					push.inv_resolution.x = 1.0f / float(push.resolution.x);
					push.inv_resolution.y = 1.0f / float(push.resolution.y);
					cmd.pushConstants<DwtPushData>(*dwt_.layout, vk::ShaderStageFlagBits::eCompute, 0, push);
				}
				else
				{
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *dwt_.layout, 0, dwt_ds[c][output_level], {});
					cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *dwt_.pipeline);
				}
				cmd.dispatch((push.aligned_resolution.x + 31) / 32, (push.aligned_resolution.y + 31) / 32, 1);
				end_label(cmd);
			}
		}

		// cmd.barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
		vk::MemoryBarrier barrier{
		        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
		        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
		};
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eComputeShader,
		        vk::PipelineStageFlagBits::eTransfer | // FIXME: remove
		                vk::PipelineStageFlagBits::eComputeShader,
		        {},
		        barrier,
		        {},
		        {});
	}

	// auto end_dwt = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	// device->register_time_interval("GPU", std::move(start_dwt), std::move(end_dwt), "DWT");
	return true;
}

size_t Encoder::compute_num_packets(const void * meta_, size_t packet_boundary) const
{
	auto * meta = static_cast<const BitstreamPacket *>(meta_);
	size_t num_packets = 0;
	size_t size_in_packet = 0;

	size_in_packet += sizeof(BitstreamSequenceHeader);

	for (int i = 0; i < block_count_32x32; i++)
	{
		size_t packet_size = meta[i].num_words * sizeof(uint32_t);
		if (!packet_size)
			continue;

		if (size_in_packet + packet_size > packet_boundary)
		{
			size_in_packet = 0;
			num_packets++;
		}

		size_in_packet += packet_size;
	}

	if (size_in_packet)
		num_packets++;

	return num_packets;
}

bool Encoder::validate_bitstream(
        const uint32_t * bitstream_u32,
        const BitstreamPacket * meta,
        uint32_t block_index) const
{
	if (meta[block_index].num_words == 0)
		return true;

	bitstream_u32 += meta[block_index].offset_u32;
	auto * header = reinterpret_cast<const BitstreamHeader *>(bitstream_u32);
	if (header->block_index != block_index)
	{
		std::cerr << "Mismatch in block index. header: " << header->block_index << ", meta: " << block_index << std::endl;
		return false;
	}

	if (header->payload_words != meta[block_index].num_words)
	{
		std::cerr << "Mismatch in payload words, header: " << header->payload_words << ", meta:}" << meta[block_index].num_words << std::endl;
		return false;
	}

	// 32x32 block layout:
	// N = popcount(ballot)
	// N * u16 control words. 2 bits per active 4x2 block.
	// N * u8 control words. 4 bits Q, 4 bits quant scale.
	// Plane data: M * u8.
	// Tightly packed sign data follows. Depends on number of significant values while decoding plane data.

	int blocks_8x8 = int(popcount32(header->ballot));
	auto * bitstream_u8 = reinterpret_cast<const uint8_t *>(bitstream_u32);
	auto * block_control_words = reinterpret_cast<const uint16_t *>(bitstream_u32 + 2);
	auto * q_control_words = reinterpret_cast<const uint8_t *>(block_control_words + blocks_8x8);
	uint32_t offset = sizeof(BitstreamHeader) + 3 * blocks_8x8;

	if (offset > header->payload_words * 4)
	{
		std::cerr << "payload_words is not large enough." << std::endl;
		return false;
	}

	const auto & mapping = block_32x32_to_8x8_mapping[header->block_index];
	bool invalid_packet = false;
	int num_significant_values = 0;

	for_each_bit(header->ballot, [&](unsigned bit) {
		int x = int(bit & 3);
		int y = int(bit >> 2);

		if (x < mapping.block_width_8x8 && y < mapping.block_height_8x8)
		{
			auto q_bits = *q_control_words & 0xf;

			for (int subblock_offset = 0; subblock_offset < 16; subblock_offset += 2)
			{
				int num_planes = q_bits + ((*block_control_words >> subblock_offset) & 3);
				int plane_significance = 0;
				for (int plane = 0; plane < num_planes; plane++)
					plane_significance |= bitstream_u8[offset++];
				num_significant_values += int(popcount32(plane_significance));
			}

			block_control_words++;
			q_control_words++;
		}
		else
		{
			std::cerr << "block_index " << block_index << ": 8x8 block is out of bounds. (" << x << ", " << y << ") >= (" << mapping.block_width_8x8 << ", " << mapping.block_height_8x8 << ")" << std::endl;
			invalid_packet = true;
		}
	});

	if (invalid_packet)
		return false;

	// We expect this many sign bits to have come through.
	offset += (num_significant_values + 7) / 8;

	auto offset_words = (offset + 3) / 4;

	if (offset_words != header->payload_words)
	{
		std::cerr << "Block index " << block_index << ", offset " << offset_words << " != " << header->payload_words << std::endl;
		return false;
	}

	return true;
}

size_t Encoder::packetize(Packet * packets, size_t packet_boundary, void * output_bitstream_, size_t size, const void * mapped_meta, const void * mapped_bitstream) const
{
	size_t num_packets = 0;
	size_t size_in_packet = 0;
	size_t packet_offset = 0;
	size_t output_offset = 0;
	auto * meta = static_cast<const BitstreamPacket *>(mapped_meta);
	auto * input_bitstream = static_cast<const uint32_t *>(mapped_bitstream);
	auto * output_bitstream = static_cast<uint8_t *>(output_bitstream_);
	(void)size;

	size_t num_non_zero_blocks = 0;
	for (int i = 0; i < block_count_32x32; i++)
		if (meta[i].num_words != 0)
			num_non_zero_blocks++;

	BitstreamSequenceHeader header = {};
	header.width_minus_1 = width - 1;
	header.height_minus_1 = height - 1;
	header.sequence = reinterpret_cast<const BitstreamHeader *>(input_bitstream + meta[0].offset_u32)->sequence;
	header.extended = 1;
	header.code = BITSTREAM_EXTENDED_CODE_START_OF_FRAME;
	header.total_blocks = num_non_zero_blocks;
	header.chroma_resolution = chroma == ChromaSubsampling::Chroma444 ? CHROMA_RESOLUTION_444 : CHROMA_RESOLUTION_420;

	assert(sizeof(header) <= size);
	memcpy(output_bitstream, &header, sizeof(header));
	output_offset += sizeof(header);
	size_in_packet += sizeof(header);

	// for (int i = 0; i < block_count_32x32; i++)
	//	if (!validate_bitstream(input_bitstream, meta, i))
	//		return false;

	for (int i = 0; i < block_count_32x32; i++)
	{
		size_t packet_size = meta[i].num_words * sizeof(uint32_t);
		if (!packet_size)
			continue;

		if (size_in_packet + packet_size > packet_boundary)
		{
			packets[num_packets++] = {packet_offset, size_in_packet};
			size_in_packet = 0;
			packet_offset = output_offset;
		}

		assert(output_offset + packet_size <= size);
		assert(packet_size >= sizeof(BitstreamHeader) / sizeof(uint32_t));

		uint16_t block = reinterpret_cast<const BitstreamHeader *>(input_bitstream + meta[i].offset_u32)->block_index;
		(void)block;
		assert(block == i);

		memcpy(output_bitstream + output_offset, input_bitstream + meta[i].offset_u32, packet_size);

		output_offset += packet_size;
		size_in_packet += packet_size;
	}

	if (size_in_packet)
		packets[num_packets++] = {packet_offset, size_in_packet};

	return num_packets;
}

bool Encoder::encode_quant_and_coding(
        vk::raii::CommandBuffer & cmd,
        const BitstreamBuffers & buffers,
        float quant_scale)
{
	// cmd.enable_subgroup_size_control(true);

	if (!quant(cmd, quant_scale))
		return false;

	if (!analyze_rdo(cmd))
		return false;

	if (!resolve_rdo(cmd, buffers.target_size))
		return false;

	if (!block_packing(cmd, buffers, quant_scale))
		return false;

	// cmd.enable_subgroup_size_control(false);
	return true;
}

bool Encoder::encode_pre_transformed(
        vk::raii::CommandBuffer & cmd,
        const BitstreamBuffers & buffers,
        float quant_scale)
{
	cmd.fillBuffer(payload_data, 0, 2 * sizeof(uint32_t), 0);
	cmd.fillBuffer(bucket_buffer, 0, vk::WholeSize, 0);
	cmd.fillBuffer(quant_buffer, 0, vk::WholeSize, 0);

	// Don't need to read the payload offset counter until quantizer.
	std::array barriers{
	        vk::BufferMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	                .dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead,
	                .buffer = payload_data,
	                .size = vk::WholeSize,
	        },
	        vk::BufferMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	                .dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead,
	                .buffer = bucket_buffer,
	                .size = vk::WholeSize,
	        },
	        vk::BufferMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	                .dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead,
	                .buffer = quant_buffer,
	                .size = vk::WholeSize,
	        },
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eAllCommands,
	        vk::PipelineStageFlagBits::eComputeShader, // FIXME: clear and copy bits
	        {},
	        {},
	        barriers,
	        {});
	// cmd.barrier(VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

	return encode_quant_and_coding(cmd, buffers, quant_scale);
}

bool Encoder::encode(vk::raii::CommandBuffer & cmd, const ViewBuffers & views, const BitstreamBuffers & buffers)
{
	// TODO: update descriptor sets
	sequence_count = (sequence_count + 1) & SequenceCountMask;

	// cmd.image_barrier(*wavelet_img_high_res, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

	vk::ImageMemoryBarrier image_barrier{
	        .srcAccessMask = vk::AccessFlagBits::eNone,
	        .dstAccessMask = vk::AccessFlagBits::eShaderWrite,
	        .oldLayout = vk::ImageLayout::eUndefined,
	        .newLayout = vk::ImageLayout::eGeneral,
	        .image = wavelet_img_high_res,
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .levelCount = VK_REMAINING_MIP_LEVELS,
	                .layerCount = VK_REMAINING_ARRAY_LAYERS,
	        },
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        {},
	        {},
	        image_barrier);

	if (wavelet_img_low_res)
	{
		// cmd.image_barrier(*wavelet_img_low_res, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
		image_barrier.image = vk::Image(wavelet_img_low_res);
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eComputeShader,
		        vk::PipelineStageFlagBits::eComputeShader,
		        {},
		        {},
		        {},
		        image_barrier);
	}

	cmd.fillBuffer(payload_data, 0, 2 * sizeof(uint32_t), 0);
	cmd.fillBuffer(bucket_buffer, 0, vk::WholeSize, 0);
	cmd.fillBuffer(quant_buffer, 0, vk::WholeSize, 0);

	// cmd.enable_subgroup_size_control(true);
	if (!dwt(cmd, views))
		return false;
	// cmd.enable_subgroup_size_control(false);

	// Don't need to read the payload offset counter until quantizer.
	// cmd.barrier(VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
	std::array barriers{
	        vk::BufferMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	                .dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead,
	                .buffer = payload_data,
	                .size = vk::WholeSize,
	        },
	        vk::BufferMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	                .dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead,
	                .buffer = bucket_buffer,
	                .size = vk::WholeSize,
	        },
	        vk::BufferMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
	                .dstAccessMask = vk::AccessFlagBits::eShaderWrite | vk::AccessFlagBits::eShaderRead,
	                .buffer = quant_buffer,
	                .size = vk::WholeSize,
	        },
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eAllCommands,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        {},
	        barriers,
	        {});

	return encode_quant_and_coding(cmd, buffers, -1.0f);
}

const vk::ImageView Encoder::get_wavelet_band(int component, int level)
{
	return *component_layer_views[component][level];
}

void Encoder::report_stats(const void *, const void *) const
{
	// impl->report_stats(mapped_meta, mapped_bitstream);
}

uint64_t Encoder::get_meta_required_size() const
{
	return block_count_32x32 * sizeof(BitstreamPacket);
}

Encoder::~Encoder()
{
}
} // namespace PyroWave
