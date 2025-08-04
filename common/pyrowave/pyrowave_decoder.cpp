// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#include "pyrowave_decoder.h"

#include <algorithm>
#include <format>
#include <glm/glm.hpp>
#include <iostream>
#include <ranges>

#define XSTR(s) STR(s)
#define STR(s) #s

namespace PyroWave
{

struct DequantizerPushData
{
	glm::ivec2 resolution;
	int32_t output_layer;
	int32_t block_offset_32x32;
	int32_t block_stride_32x32;
};
struct IDwtPushData
{
	glm::ivec2 resolution;
	glm::vec2 inv_resolution;
};
static vk::raii::DescriptorPool make_descriptor_pool(vk::raii::Device & device)
{
	std::array pool_sizes{
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageBuffer,
	                .descriptorCount =
	                        2 * NumComponents * DecompositionLevels, // dequant
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = 2 * NumComponents * DecompositionLevels, // dequant + iwt
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageImage,
	                .descriptorCount = NumComponents * DecompositionLevels, // idwt
	        },
	};
	return {device,
	        vk::DescriptorPoolCreateInfo{
	                .maxSets = 2 * NumComponents * DecompositionLevels, // dequant + iwt
	                .poolSizeCount = pool_sizes.size(),
	                .pPoolSizes = pool_sizes.data(),
	        }};
}

DecoderInput::DecoderInput(const Decoder & decoder) :
        decoder(decoder)
{
	vk::BufferCreateInfo info{
	        .size = decoder.block_count_32x32 * sizeof(uint32_t),
	        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
	};
	dequant_offset_buffer = buffer_allocation(
	        decoder.device,
	        info,
	        {
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        "dequant offset buffer");
	if (dequant_offset_buffer.properties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
	{
		dequant_data = std::span((uint32_t *)dequant_offset_buffer.map(), decoder.block_count_32x32);
	}
	else
	{
		info.usage = vk::BufferUsageFlagBits::eTransferSrc;
		dequant_staging = buffer_allocation(
		        decoder.device,
		        info,
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        },
		        "dequant staging");
		dequant_data = std::span((uint32_t *)dequant_staging.map(), decoder.block_count_32x32);
	}

	clear();
}

void DecoderInput::push_raw(const void * data, size_t size)
{
	vk::DeviceSize required_size = this->payload_size + size;
	if (required_size > payload_data.info().size)
	{
		vk::DeviceSize required_size_padded = required_size + 16;
		vk::BufferCreateInfo info{
		        .size = std::max<VkDeviceSize>(64 * 1024, required_size_padded * 2),
		        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
		};
		buffer_allocation buf(
		        decoder.device,
		        info,
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        });
		if (buf.properties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
		{
			memcpy(buf.map(), payload, payload_data.info().size);
			payload = (uint8_t *)buf.map();
		}
		else
		{
			buffer_allocation staging(
			        decoder.device,
			        info,
			        {
			                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
			                .usage = VMA_MEMORY_USAGE_AUTO,
			        });
			memcpy(staging.map(), payload, payload_data.info().size);
			payload = (uint8_t *)staging.map();
			std::swap(staging, payload_staging);
		}
		std::swap(buf, payload_data);
	}
	memcpy(payload + this->payload_size, data, size);
	this->payload_size += size;
}

void DecoderInput::clear()
{
	std::ranges::fill(dequant_data, UINT32_MAX);
	decoded_blocks = 0;
	total_blocks_in_sequence = decoder.block_count_32x32;
	payload_size = 0;
	header_size = 0;
	packet_size = 0;
}

bool DecoderInput::push_data(std::span<const uint8_t> data)
{
	while (not data.empty())
	{
		if (header_size < sizeof(BitstreamHeader))
		{
			size_t s = std::min(data.size_bytes(), sizeof(BitstreamHeader) - header_size);
			std::memcpy(((uint8_t *)&header) + header_size, data.data(), s);
			header_size += s;
			data = data.subspan(s);

			if (header_size < sizeof(BitstreamHeader))
				break;

			if (not header.extended)
			{
				bool restart;

				if (last_seq == UINT32_MAX)
					restart = true;
				else
				{
					uint8_t diff = (header.sequence - last_seq) & SequenceCountMask;
					if (diff > (SequenceCountMask / 2))
					{
						// All sequences in a packet must be the same.
						std::cerr << "Backwards sequence detected, discarding." << std::endl;
						return true;
					}
					restart = diff != 0;
				}

				if (restart)
				{
					clear();
					last_seq = header.sequence;
				}

				if (header.block_index >= uint32_t(decoder.block_count_32x32))
				{
					std::cerr << "block_index " << header.block_index << " is out of bounds (>= " << decoder.block_count_32x32 << ")." << std::endl;
					return false;
				}

				auto & offset = dequant_data[header.block_index];
				if (offset == UINT32_MAX)
				{
					decoded_blocks++;
					assert(payload_size % sizeof(uint32_t) == 0);
					offset = payload_size / sizeof(uint32_t);
				}
				else
				{
					std::cerr << "block_index " << header.block_index << " is already decoded, skipping." << std::endl;
					return true;
				}

				push_raw(&header, sizeof(BitstreamHeader));
				packet_size = header.payload_words * sizeof(uint32_t) - sizeof(BitstreamHeader);
			}
		}

		if (header.extended != 0)
		{
			header_size = 0;
			const auto & seq = reinterpret_cast<const BitstreamSequenceHeader &>(header);

			if (seq.chroma_resolution != int(decoder.chroma))
			{
				std::cerr << "Chroma resolution mismatch!" << std::endl;
				return false;
			}

			uint8_t diff = (header.sequence - last_seq) & SequenceCountMask;
			if (last_seq != UINT32_MAX && diff > (SequenceCountMask / 2))
			{
				// All sequences in a packet must be the same.
				std::cerr << "Backwards sequence detected, discarding." << std::endl;
				return true;
			}

			if (last_seq == UINT32_MAX || diff != 0)
			{
				clear();
				last_seq = header.sequence;
			}

			if (seq.code == BITSTREAM_EXTENDED_CODE_START_OF_FRAME)
			{
				if (seq.width_minus_1 + 1 != decoder.width || seq.height_minus_1 + 1 != decoder.height)
				{
					std::cerr << "Dimension mismatch in seq packet, (" << seq.width_minus_1 + 1 << ", " << seq.height_minus_1 + 1 << ") != (" << decoder.width << ", " << decoder.height << ")" << std::endl;
					return false;
				}

				total_blocks_in_sequence = int(seq.total_blocks);
			}
			else
			{
				std::cerr << "Unrecognized sequence header mode " << seq.code << "." << std::endl;
				return false;
			}
		}
		else
		{
			size_t s = std::min(packet_size, data.size_bytes());
			push_raw(data.data(), s);

			data = data.subspan(s);
			packet_size -= s;
			if (packet_size == 0)
				header_size = 0;
		}
	}

	return true;
}

Decoder::Decoder(vk::raii::PhysicalDevice & phys_dev, vk::raii::Device & device, int width, int height, ChromaSubsampling chroma) :
        WaveletBuffers(device, width, height, chroma),
        ds_pool(make_descriptor_pool(device))
{
	auto [prop, prop11, prop13] = phys_dev.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan11Properties, vk::PhysicalDeviceVulkan13Properties>();
	auto ops = prop11.subgroupSupportedOperations;
	constexpr auto required_features =
	        vk::SubgroupFeatureFlagBits::eVote |
	        vk::SubgroupFeatureFlagBits::eQuad |
	        vk::SubgroupFeatureFlagBits::eBallot |
	        vk::SubgroupFeatureFlagBits::eArithmetic |
	        vk::SubgroupFeatureFlagBits::eShuffle |
	        vk::SubgroupFeatureFlagBits::eShuffleRelative |
	        vk::SubgroupFeatureFlagBits::eBasic;

	if ((ops & required_features) != required_features)
	{
		throw std::runtime_error("There are missing subgroup features. Device supports " + vk::to_string(ops) + ", but requires " + vk::to_string(required_features) + ".");
	}

	auto [feat, feat12, feat13] = phys_dev.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();

	// The decoder is more lenient.
	if (!supports_subgroup_size_log2(prop13, true, 4, 6))
		throw std::runtime_error("Device does not have the required subgroup properties");

	if (not feat12.storageBuffer8BitAccess)
		throw std::runtime_error("Missing storageBuffer8BitAccess feature");

	if (not feat12.shaderFloat16)
		throw std::runtime_error("Missing shaderFloat16 feature");

	init_block_meta();

	// dequant pipeline
	{
		std::array bindings{
		        vk::DescriptorSetLayoutBinding{
		                .binding = 0,
		                .descriptorType = vk::DescriptorType::eStorageImage,
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
		};
		dequant_.ds_layout = device.createDescriptorSetLayout({
		        .bindingCount = bindings.size(),
		        .pBindings = bindings.data(),
		});

		vk::PushConstantRange pc{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .size = sizeof(DequantizerPushData),
		};

		dequant_.layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*dequant_.ds_layout,
		        .pushConstantRangeCount = 1,
		        .pPushConstantRanges = &pc,
		});

		vk::PipelineShaderStageRequiredSubgroupSizeCreateInfoEXT subgroup_size{};
		auto shader = load_shader(device, "wavelet_dequant");
		vk::ComputePipelineCreateInfo info{
		        .stage = {
		                .flags = vk::PipelineShaderStageCreateFlagBits::eRequireFullSubgroups,
		                .stage = vk::ShaderStageFlagBits::eCompute,
		                .module = *shader,
		                .pName = "main",
		        },
		        .layout = *dequant_.layout,
		};
		pipeline_subgroup_info psi;
		psi.set_subgroup_size(prop13, info, 4, 6);
		dequant_.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		for (int level = 0; level < DecompositionLevels; level++)
		{
			for (int component = 0; component < NumComponents; component++)
			{
				dequant_.ds[component][level] = device.allocateDescriptorSets({
				        .descriptorPool = *ds_pool,
				        .descriptorSetCount = 1,
				        .pSetLayouts = &*dequant_.ds_layout,
				})[0]
				                                        .release();
				vk::DescriptorImageInfo image_info{
				        .sampler = *border_sampler,
				        .imageView = *component_layer_views[component][level],
				        .imageLayout = vk::ImageLayout::eGeneral,
				};

				device.updateDescriptorSets(vk::WriteDescriptorSet{
				                                    .dstSet = dequant_.ds[component][level],
				                                    .dstBinding = 0,
				                                    .descriptorCount = 1,
				                                    .descriptorType = vk::DescriptorType::eStorageImage,
				                                    .pImageInfo = &image_info,
				                            },
				                            {});
			}
		}
	}

	// idwt pipeline
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
		idwt_.ds_layout = device.createDescriptorSetLayout({
		        .bindingCount = bindings.size(),
		        .pBindings = bindings.data(),
		});

		vk::PushConstantRange pc{
		        .stageFlags = vk::ShaderStageFlagBits::eCompute,
		        .size = sizeof(IDwtPushData),
		};

		idwt_.layout = device.createPipelineLayout({
		        .setLayoutCount = 1,
		        .pSetLayouts = &*idwt_.ds_layout,
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
		auto shader = load_shader(device, "idwt_" XSTR(PYROWAVE_PRECISION));
		vk::ComputePipelineCreateInfo info{
		        .stage = {
		                .flags = vk::PipelineShaderStageCreateFlagBits::eRequireFullSubgroups,
		                .stage = vk::ShaderStageFlagBits::eCompute,
		                .module = *shader,
		                .pName = "main",
		                .pSpecializationInfo = &sp,
		        },
		        .layout = *idwt_.layout,
		};
		pipeline_subgroup_info psi;
		psi.set_subgroup_size(prop13, info, 2, 6);
		idwt_.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		dc_shift = true;
		idwt_dcshift = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		for (int input_level = DecompositionLevels - 1; input_level >= 0; input_level--)
		{
			for (int c = 0; c < NumComponents; c++)
			{
				idwt_.ds[c][input_level] = device.allocateDescriptorSets({
				        .descriptorPool = *ds_pool,
				        .descriptorSetCount = 1,
				        .pSetLayouts = &*idwt_.ds_layout,
				})[0]
				                                   .release();

				vk::DescriptorImageInfo texture{
				        .sampler = *mirror_repeat_sampler,
				        .imageView = *component_layer_views[c][input_level],
				        .imageLayout = vk::ImageLayout::eGeneral,
				};
				vk::WriteDescriptorSet descriptor_write{
				        .dstSet = idwt_.ds[c][input_level],
				        .dstBinding = 0,
				        .descriptorCount = 1,
				        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
				        .pImageInfo = &texture,
				};
				device.updateDescriptorSets(descriptor_write, {});

				// Updated with output image
				if (input_level == 0)
					continue;
				if (input_level == 1 and c > 0 and chroma == ChromaSubsampling::Chroma420)
					continue;

				vk::DescriptorImageInfo storage{
				        .imageView = *component_ll_views[c][input_level - 1],
				        .imageLayout = vk::ImageLayout::eGeneral,
				};

				descriptor_write = {
				        .dstSet = idwt_.ds[c][input_level],
				        .dstBinding = 1,
				        .descriptorCount = 1,
				        .descriptorType = vk::DescriptorType::eStorageImage,
				        .pImageInfo = &storage,
				};

				device.updateDescriptorSets(descriptor_write, {});
			}
		}
	}
}

Decoder::~Decoder()
{
}

bool Decoder::dequant(vk::raii::CommandBuffer & cmd)
{
	DequantizerPushData push = {};

	cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *dequant_.pipeline);
	begin_label(cmd, "DWT dequant");
	// auto start_dequant = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

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
		image_barrier.image = vk::Image(wavelet_img_low_res);
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eComputeShader,
		        vk::PipelineStageFlagBits::eComputeShader,
		        {},
		        {},
		        {},
		        image_barrier);
	}

	// De-quantize
	for (int level = 0; level < DecompositionLevels; level++)
	{
		for (int component = 0; component < NumComponents; component++)
		{
			// Ignore top-level CbCr when doing 420 subsampling.
			if (level == 0 && component != 0 && chroma == ChromaSubsampling::Chroma420)
				continue;

			begin_label(cmd, std::format("level {} - component {}", level, component).c_str());

			for (int band = (level == DecompositionLevels - 1 ? 0 : 1); band < 4; band++)
			{
				push.resolution.x = get_width(wavelet_img_high_res, level);
				push.resolution.y = get_width(wavelet_img_high_res, level);
				push.output_layer = band;
				push.block_offset_32x32 = block_meta[component][level][band].block_offset_32x32;
				push.block_stride_32x32 = block_meta[component][level][band].block_stride_32x32;
				cmd.pushConstants<DequantizerPushData>(*dequant_.layout, vk::ShaderStageFlagBits::eCompute, 0, push);

				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *dequant_.layout, 0, dequant_.ds[component][level], {});
				cmd.dispatch((push.resolution.x + 31) / 32, (push.resolution.y + 31) / 32, 1);
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
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        barrier,
	        {},
	        {});

	// auto end_dequant = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	end_label(cmd);
	// device->register_time_interval("GPU", std::move(start_dequant), std::move(end_dequant), "Dequant");

	return true;
}

bool Decoder::idwt(vk::raii::CommandBuffer & cmd, const ViewBuffers & views)
{
	// auto start_idwt = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	IDwtPushData push{};

	for (int input_level = DecompositionLevels - 1; input_level >= 0; input_level--)
	{
		// Transposed.
		push.resolution.x = component_ll_dim[0][input_level].height;
		push.resolution.y = component_ll_dim[0][input_level].width;
		push.inv_resolution.x = 1.0f / float(push.resolution.x);
		push.inv_resolution.y = 1.0f / float(push.resolution.y);
		cmd.pushConstants<IDwtPushData>(*idwt_.layout, vk::ShaderStageFlagBits::eCompute, 0, push);

		if (input_level == 0)
		{
			cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *idwt_dcshift);
			if (chroma == ChromaSubsampling::Chroma444)
			{
				for (int c = 0; c < NumComponents; c++)
				{
					begin_label(cmd, std::format("iDWT final, component {}", c).c_str());
					vk::DescriptorImageInfo storage{
					        .imageView = views[c],
					        .imageLayout = vk::ImageLayout::eGeneral,
					};
					vk::WriteDescriptorSet descriptor_write{
					        .dstSet = idwt_.ds[c][input_level],
					        .dstBinding = 1,
					        .descriptorCount = 1,
					        .descriptorType = vk::DescriptorType::eStorageImage,
					        .pImageInfo = &storage,
					};
					device.updateDescriptorSets(descriptor_write, {});
					cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *idwt_.layout, 0, idwt_.ds[c][input_level], {});
					cmd.dispatch((push.resolution.x + 15) / 16, (push.resolution.y + 15) / 16, 1);
					end_label(cmd);
				}
			}
			else
			{
				begin_label(cmd, "iDWT final");
				vk::DescriptorImageInfo storage{
				        .imageView = views[0],
				        .imageLayout = vk::ImageLayout::eGeneral,
				};
				vk::WriteDescriptorSet descriptor_write{
				        .dstSet = idwt_.ds[0][input_level],
				        .dstBinding = 1,
				        .descriptorCount = 1,
				        .descriptorType = vk::DescriptorType::eStorageImage,
				        .pImageInfo = &storage,
				};
				device.updateDescriptorSets(descriptor_write, {});
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *idwt_.layout, 0, idwt_.ds[0][input_level], {});
				cmd.dispatch((push.resolution.x + 15) / 16, (push.resolution.y + 15) / 16, 1);
				end_label(cmd);
			}
		}
		else
		{
			for (int c = 0; c < NumComponents; c++)
			{
				begin_label(cmd, std::format("iDWT level {}, component {}", input_level, c).c_str());

				if (chroma == ChromaSubsampling::Chroma420 && c != 0 && input_level == 1)
				{
					vk::DescriptorImageInfo storage{
					        .imageView = views[c],
					        .imageLayout = vk::ImageLayout::eGeneral,
					};
					vk::WriteDescriptorSet descriptor_write{
					        .dstSet = idwt_.ds[c][input_level],
					        .dstBinding = 1,
					        .descriptorCount = 1,
					        .descriptorType = vk::DescriptorType::eStorageImage,
					        .pImageInfo = &storage,
					};
					device.updateDescriptorSets(descriptor_write, {});
					cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *idwt_dcshift);
				}
				else
				{
					cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *idwt_.pipeline);
				}

				cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *idwt_.layout, 0, idwt_.ds[c][input_level], {});
				cmd.dispatch((push.resolution.x + 15) / 16, (push.resolution.y + 15) / 16, 1);
				end_label(cmd);
			}
		}

		vk::MemoryBarrier barrier{
		        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
		        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
		};
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eComputeShader,
		        vk::PipelineStageFlagBits::eComputeShader,
		        {},
		        barrier,
		        {},
		        {});
	}

	// auto end_idwt = cmd.write_timestamp(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
	// device->register_time_interval("GPU", std::move(start_idwt), std::move(end_idwt), "iDWT");
	return true;
}

bool Decoder::decode(vk::raii::CommandBuffer & cmd, DecoderInput & input, const ViewBuffers & views)
{
	for (int level = 0; level < DecompositionLevels; level++)
	{
		for (int component = 0; component < NumComponents; component++)
		{
			std::array buffer_info{
			        vk::DescriptorBufferInfo{
			                .buffer = input.dequant_offset_buffer,
			                .range = vk::WholeSize,
			        },
			        vk::DescriptorBufferInfo{
			                .buffer = input.payload_data,
			                .range = vk::WholeSize,
			        },
			};

			device.updateDescriptorSets(
			        vk::WriteDescriptorSet{
			                .dstSet = dequant_.ds[component][level],
			                .dstBinding = 1,
			                .descriptorCount = buffer_info.size(),
			                .descriptorType = vk::DescriptorType::eStorageBuffer,
			                .pBufferInfo = buffer_info.data(),
			        },
			        {});
		}
	}
	begin_label(cmd, "Decode uploads");
	{
		if (input.payload_staging)
			cmd.copyBuffer(
			        input.payload_staging,
			        input.payload_data,
			        vk::BufferCopy{.size = input.payload_size});

		if (input.dequant_staging)
			cmd.copyBuffer(
			        input.dequant_staging,
			        input.dequant_offset_buffer,
			        vk::BufferCopy{
			                .size = vk::WholeSize,
			        });

		vk::MemoryBarrier barrier{
		        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
		        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
		};
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eTransfer,
		        vk::PipelineStageFlagBits::eComputeShader,
		        {},
		        barrier,
		        {},
		        {});
	}
	end_label(cmd);

	if (!dequant(cmd))
		return false;

	vk::MemoryBarrier barrier{
	        .srcAccessMask = vk::AccessFlagBits::eNone,
	        .dstAccessMask = vk::AccessFlagBits::eNone,
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eComputeShader,
	        vk::PipelineStageFlagBits::eTransfer,
	        {},
	        barrier,
	        {},
	        {});

	if (!idwt(cmd, views))
		return false;

	return true;
}

} // namespace PyroWave
