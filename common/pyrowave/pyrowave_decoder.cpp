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
struct IDwtFragmentPushData
{
	glm::vec2 uv_offset;
	glm::vec2 half_texel_offset;
	float vp_scale;
	uint32_t pivot_size;
};
static vk::raii::DescriptorPool make_descriptor_pool(vk::raii::Device & device, bool readonly_texel_buffer, bool fragment)
{
	uint32_t dequant = DecompositionLevels * NumComponents;
	uint32_t idwt = fragment ? 0 : (DecompositionLevels * NumComponents);
	uint32_t f_idwt = fragment ? (DecompositionLevels * 3 + 1) : 0;
	std::array pool_sizes{
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageBuffer,
	                .descriptorCount =
	                        dequant * (readonly_texel_buffer ? 1 : 2), // dequant
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eCombinedImageSampler,
	                .descriptorCount = idwt,
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eStorageImage,
	                .descriptorCount = dequant + idwt,
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eUniformTexelBuffer,
	                .descriptorCount =
	                        3 * dequant * readonly_texel_buffer, // dequant
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eSampler,
	                .descriptorCount = f_idwt, // idwt
	        },
	        vk::DescriptorPoolSize{
	                .type = vk::DescriptorType::eSampledImage,
	                .descriptorCount = f_idwt * 6, // idwt
	        },
	};
	auto range = std::ranges::remove_if(pool_sizes, [](auto & s) { return s.descriptorCount == 0; });
	return {device,
	        vk::DescriptorPoolCreateInfo{
	                .maxSets = dequant + idwt + f_idwt,
	                .poolSizeCount = uint32_t(std::distance(pool_sizes.begin(), range.begin())),
	                .pPoolSizes = pool_sizes.data(),
	        }};
}

DecoderInput::DecoderInput(const Decoder & decoder) :
        decoder(decoder)
{
	auto & device = decoder.device;
	vk::BufferCreateInfo info{
	        .size = decoder.block_count_32x32 * sizeof(uint32_t),
	        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
	};
	dequant_offset_buffer = buffer_allocation(
	        device,
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
		        device,
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
		auto & device = decoder.device;
		vk::DeviceSize required_size_padded = required_size + 16;
		vk::BufferCreateInfo info{
		        .size = std::max<VkDeviceSize>(64 * 1024, required_size_padded * 2),
		        .usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
		};
		if (decoder.use_readonly_texel_buffer)
			info.usage |= vk::BufferUsageFlagBits::eUniformTexelBuffer;
		buffer_allocation buf(
		        device,
		        info,
		        {
		                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
		                .usage = VMA_MEMORY_USAGE_AUTO,
		        });
		if (decoder.use_readonly_texel_buffer)
		{
			vk::BufferViewCreateInfo info{
			        .buffer = buf,
			        .range = vk::WholeSize,
			};
			info.format = vk::Format::eR32Uint;
			u32_view = device.createBufferView(info);
			info.format = vk::Format::eR16Uint;
			u16_view = device.createBufferView(info);
			info.format = vk::Format::eR8Uint;
			u8_view = device.createBufferView(info);
		}
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
	last_seq = UINT32_MAX;
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

Decoder::Decoder(
        vk::raii::PhysicalDevice & phys_dev,
        vk::raii::Device & device,
        int width,
        int height,
        ChromaSubsampling chroma,
        bool fragment_path) :
        WaveletBuffers(device, width, height, chroma),
        fragment_path(fragment_path),
        ds_pool(nullptr)
{
	auto [prop, prop11, prop13] = phys_dev.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan11Properties, vk::PhysicalDeviceVulkan13Properties>();
	auto ops = prop11.subgroupSupportedOperations;
	constexpr auto required_features =
	        vk::SubgroupFeatureFlagBits::eVote |
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
	if (!supports_subgroup_size_log2(prop13, true, 2, 7))
		throw std::runtime_error("Device does not have the required subgroup properties");

	// If the GPU is sufficiently competent with texel buffers, we can use that as a fallback to 8-bit storage.
	if (prop.properties.limits.maxTexelBufferElements >= 16 * 1024 * 1024)
	{
		auto vendor_id = prop.properties.vendorID;

		if (not feat12.storageBuffer8BitAccess or
		    (vendor_id != 0x1002 /*AMD*/ and
		     vendor_id != 0x8086 /*Intel*/ and
		     vendor_id != 0x10de /*NVIDIA*/))
		{
			use_readonly_texel_buffer = true;
		}
	}

	if (not(feat12.storageBuffer8BitAccess or use_readonly_texel_buffer))
		throw std::runtime_error("Missing storageBuffer8BitAccess feature");

	ds_pool = make_descriptor_pool(device, use_readonly_texel_buffer, fragment_path);

	if (fragment_path)
	{
		auto format = PYROWAVE_PRECISION == 2 ? vk::Format::eR32Sfloat : vk::Format::eR16Sfloat;
		auto vert_chroma_format = PYROWAVE_PRECISION == 2 ? vk::Format::eR32G32Sfloat : vk::Format::eR16G16Sfloat;
		for (int level = 0; level < DecompositionLevels; level++)
		{
			uint32_t horiz_output_width = aligned_width >> (level + 1);
			uint32_t horiz_output_height = aligned_height >> (level + 1);
			uint32_t vert_input_width = horiz_output_width;
			uint32_t vert_input_height = horiz_output_height * 2;

			for (int comp = 0; comp < 3; comp++)
			{
				vk::ImageCreateInfo info{
				        .imageType = vk::ImageType::e2D,
				        .format = format,
				        .extent = {.depth = 1},
				        .mipLevels = 1,
				        .arrayLayers = 1,
				        .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
				};
				info.extent.width = horiz_output_width;
				info.extent.height = horiz_output_height;
				info.format = format;
				fragment.levels[level].horiz[comp] = image_allocation(
				        device,
				        info,
				        {.usage = VMA_MEMORY_USAGE_AUTO},
				        std::format("Horiz Output (level {}, comp {})", level, comp));
				fragment.levels[level].horiz_views[comp] = device.createImageView(vk::ImageViewCreateInfo{
				        .image = fragment.levels[level].horiz[comp],
				        .viewType = vk::ImageViewType::e2D,
				        .format = info.format,
				        .subresourceRange = {
				                .aspectMask = vk::ImageAspectFlagBits::eColor,
				                .levelCount = 1,
				                .layerCount = 1,
				        }});

				if (comp < 2)
				{
					info.extent.width = vert_input_width;
					info.extent.height = vert_input_height;
					info.format = comp == 0 ? format : vert_chroma_format;
					fragment.levels[level].vert[0][comp] = image_allocation(
					        device,
					        info,
					        {.usage = VMA_MEMORY_USAGE_AUTO},
					        std::format("Vert even input (level {}, comp {})", level, comp));
					vk::ImageViewCreateInfo view_info{
					        .image = fragment.levels[level].vert[0][comp],
					        .viewType = vk::ImageViewType::e2D,
					        .format = info.format,
					        .subresourceRange = {
					                .aspectMask = vk::ImageAspectFlagBits::eColor,
					                .levelCount = 1,
					                .layerCount = 1,
					        }};
					fragment.levels[level].vert_views[0][comp] = device.createImageView(view_info);
					fragment.levels[level].vert[1][comp] = image_allocation(
					        device,
					        info,
					        {.usage = VMA_MEMORY_USAGE_AUTO},
					        std::format("Vert odd input (level {}, comp {})", level, comp));
					view_info.image = vk::Image(fragment.levels[level].vert[1][comp]);
					fragment.levels[level].vert_views[1][comp] = device.createImageView(view_info);
				}
			}

			for (int comp = 0; comp < NumComponents; comp++)
			{
				auto & dequant_view = component_layer_views[comp][level];
				auto & dequant_view_info = component_layer_views_info[comp][level];

				for (int band = 0; band < NumFrequencyBandsPerLevel; band++)
				{
					vk::ImageViewCreateInfo view_info = {
					        .viewType = vk::ImageViewType::e2D,
					        .subresourceRange = {
					                .aspectMask = vk::ImageAspectFlagBits::eColor,
					                .levelCount = 1,
					                .layerCount = 1,
					        }};

					image_allocation * image = nullptr;
					if (band == 0 && level < DecompositionLevels - 1)
					{
						image = &fragment.levels[level].horiz[comp];
						view_info.image = vk::Image(*image);
						view_info.format = format;
						view_info.subresourceRange.baseMipLevel = 0;
						view_info.subresourceRange.baseArrayLayer = 0;
						if (comp == 0)
						{
							fragment.levels[level].decoded_dim.width = get_width(*image, 0);
							fragment.levels[level].decoded_dim.height = get_height(*image, 0);
						}
					}
					else if (*dequant_view)
					{
						view_info.image = dequant_view_info.image;
						view_info.format = dequant_view_info.format;
						view_info.subresourceRange.baseMipLevel = dequant_view_info.subresourceRange.baseMipLevel;
						view_info.subresourceRange.baseArrayLayer = dequant_view_info.subresourceRange.baseArrayLayer + band;
						if (comp == 0 and band == 0)
							fragment.levels[level].decoded_dim = component_ll_dim[comp][level];
					}

					fragment.levels[level].decoded[comp][band] = device.createImageView(view_info);
				}
			}
		}
	}

	init_block_meta();

	// dequant pipeline
	{
		if (use_readonly_texel_buffer)
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
			                .descriptorType = vk::DescriptorType::eUniformTexelBuffer,
			                .descriptorCount = 1,
			                .stageFlags = vk::ShaderStageFlagBits::eCompute,
			        },
			        vk::DescriptorSetLayoutBinding{
			                .binding = 3,
			                .descriptorType = vk::DescriptorType::eUniformTexelBuffer,
			                .descriptorCount = 1,
			                .stageFlags = vk::ShaderStageFlagBits::eCompute,
			        },
			        vk::DescriptorSetLayoutBinding{
			                .binding = 4,
			                .descriptorType = vk::DescriptorType::eUniformTexelBuffer,
			                .descriptorCount = 1,
			                .stageFlags = vk::ShaderStageFlagBits::eCompute,
			        },
			};
			dequant_.ds_layout = device.createDescriptorSetLayout({
			        .bindingCount = bindings.size(),
			        .pBindings = bindings.data(),
			});
		}
		else
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
		}

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
		auto shader = load_shader(device, use_readonly_texel_buffer ? "wavelet_dequant" : "wavelet_dequant_8b");
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
		if (supports_subgroup_size_log2(prop13, true, 4, 7))
			psi.set_subgroup_size(prop13, info, 4, 7);
		else
			psi.set_subgroup_size(prop13, info, 2, 7);
		dequant_.pipeline = device.createComputePipeline(
		        nullptr, // FIXME: cache
		        info);

		for (int level = 0; level < DecompositionLevels; level++)
		{
			for (int component = 0; component < NumComponents; component++)
			{
				dequant_.ds[component][level] = allocate_descriptor_set(*dequant_.ds_layout);
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
	if (fragment_path)
	{
		for (size_t chroma_config = 0; chroma_config < 3; ++chroma_config)
		{
			std::vector bindings{
			        vk::DescriptorSetLayoutBinding{
			                .binding = 0,
			                .descriptorType = vk::DescriptorType::eSampledImage,
			                .descriptorCount = 1,
			                .stageFlags = vk::ShaderStageFlagBits::eFragment,
			        },
			        vk::DescriptorSetLayoutBinding{
			                .binding = 1,
			                .descriptorType = vk::DescriptorType::eSampledImage,
			                .descriptorCount = 1,
			                .stageFlags = vk::ShaderStageFlagBits::eFragment,
			        },
			        vk::DescriptorSetLayoutBinding{
			                .binding = 2,
			                .descriptorType = vk::DescriptorType::eSampler,
			                .descriptorCount = 1,
			                .stageFlags = vk::ShaderStageFlagBits::eFragment,
			        },
			};
			if (chroma_config > 0)
			{
				bindings.push_back(
				        vk::DescriptorSetLayoutBinding{
				                .binding = 3,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .descriptorCount = 1,
				                .stageFlags = vk::ShaderStageFlagBits::eFragment,
				        });
				bindings.push_back(
				        vk::DescriptorSetLayoutBinding{
				                .binding = 4,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .descriptorCount = 1,
				                .stageFlags = vk::ShaderStageFlagBits::eFragment,
				        });
			}
			if (chroma_config == 1)
			{
				bindings.push_back(
				        vk::DescriptorSetLayoutBinding{
				                .binding = 5,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .descriptorCount = 1,
				                .stageFlags = vk::ShaderStageFlagBits::eFragment,
				        });
				bindings.push_back(
				        vk::DescriptorSetLayoutBinding{
				                .binding = 6,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .descriptorCount = 1,
				                .stageFlags = vk::ShaderStageFlagBits::eFragment,
				        });
			}
			fragment.ds_layout[chroma_config] = device.createDescriptorSetLayout({
			        .bindingCount = uint32_t(bindings.size()),
			        .pBindings = bindings.data(),
			});

			vk::PushConstantRange pc{
			        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
			        .size = sizeof(IDwtFragmentPushData),
			};

			fragment.layout[chroma_config] = device.createPipelineLayout({
			        .setLayoutCount = 1,
			        .pSetLayouts = &*fragment.ds_layout[chroma_config],
			        .pushConstantRangeCount = 1,
			        .pPushConstantRanges = &pc,
			});
		}

		auto vert_shader = load_shader(device, "idwt.vert");
		vk::raii::ShaderModule frag_shader[3] = {
		        load_shader(device, "idwt_0.frag"),
		        load_shader(device, "idwt_1.frag"),
		        load_shader(device, "idwt_2.frag"),
		};

		auto ensure_render_pass = [&](const key_render_pass & render_pass_key) {
			auto it = fragment.render_pass.find(render_pass_key);
			if (it == fragment.render_pass.end())
			{
				auto & formats = std::get<0>(render_pass_key);
				auto & layouts = std::get<1>(render_pass_key);
				uint32_t colorAttachmentCount = std::ranges::count_if(formats, [](auto & f) { return f != vk::Format::eUndefined; });
				std::array attachment_ref{
				        vk::AttachmentReference{
				                .attachment = 0,
				                .layout = vk::ImageLayout::eColorAttachmentOptimal,
				        },
				        vk::AttachmentReference{
				                .attachment = 1,
				                .layout = vk::ImageLayout::eColorAttachmentOptimal,
				        },
				        vk::AttachmentReference{
				                .attachment = 2,
				                .layout = vk::ImageLayout::eColorAttachmentOptimal,
				        },
				};
				std::array attachments{
				        vk::AttachmentDescription{
				                .format = formats[0],
				                .loadOp = vk::AttachmentLoadOp::eDontCare,
				                .initialLayout = layouts[0],
				                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
				        },
				        vk::AttachmentDescription{
				                .format = formats[1],
				                .loadOp = vk::AttachmentLoadOp::eDontCare,
				                .initialLayout = layouts[1],
				                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
				        },
				        vk::AttachmentDescription{
				                .format = formats[2],
				                .loadOp = vk::AttachmentLoadOp::eDontCare,
				                .initialLayout = layouts[2],
				                .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
				        },
				};
				vk::SubpassDescription subpass{
				        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
				        .colorAttachmentCount = colorAttachmentCount,
				        .pColorAttachments = attachment_ref.data(),
				};
				vk::RenderPassCreateInfo rp_info{
				        .attachmentCount = colorAttachmentCount,
				        .pAttachments = attachments.data(),
				        .subpassCount = 1,
				        .pSubpasses = &subpass,
				};
				it = fragment.render_pass.emplace(render_pass_key, device.createRenderPass(rp_info)).first;
			}
			return *it->second;
		};
		auto ensure_pipeline = [&](const key_render_pass & render_pass_key, int chroma_config, sp sp_constants) {
			key_pipeline key{
			        ensure_render_pass(render_pass_key),
			        *fragment.layout[chroma_config],
			        sp_constants,
			};
			auto it = fragment.pipelines.find(key);
			if (it == fragment.pipelines.end())
			{
				uint32_t colorAttachmentCount = std::ranges::count_if(std::get<0>(render_pass_key), [](auto & f) { return f != vk::Format::eUndefined; });
				vk::SpecializationMapEntry vert_sp_entries{
				        .constantID = 0,
				        .offset = 0,
				        .size = sizeof(VkBool32),
				};
				vk::SpecializationInfo vert_sp_info{
				        .mapEntryCount = 1,
				        .pMapEntries = &vert_sp_entries,
				        .dataSize = sizeof(VkBool32),
				        .pData = &std::get<0>(sp_constants),
				};
				std::array frag_sp_entries{
				        vk::SpecializationMapEntry{
				                .constantID = 0,
				                .offset = uint32_t(intptr_t(&std::get<0>(sp_constants)) - intptr_t(&sp_constants)),
				                .size = sizeof(VkBool32),
				        },
				        vk::SpecializationMapEntry{
				                .constantID = 1,
				                .offset = uint32_t(intptr_t(&std::get<1>(sp_constants)) - intptr_t(&sp_constants)),
				                .size = sizeof(VkBool32),
				        },
				        vk::SpecializationMapEntry{
				                .constantID = 2,
				                .offset = uint32_t(intptr_t(&std::get<2>(sp_constants)) - intptr_t(&sp_constants)),
				                .size = sizeof(VkBool32),
				        },
				        vk::SpecializationMapEntry{
				                .constantID = 3,
				                .offset = uint32_t(intptr_t(&std::get<3>(sp_constants)) - intptr_t(&sp_constants)),
				                .size = sizeof(int32_t),
				        },
				};
				vk::SpecializationInfo frag_sp_info{
				        .mapEntryCount = frag_sp_entries.size(),
				        .pMapEntries = frag_sp_entries.data(),
				        .dataSize = sizeof(sp_constants),
				        .pData = &sp_constants,
				};

				std::array stages{
				        vk::PipelineShaderStageCreateInfo{
				                .stage = vk::ShaderStageFlagBits::eVertex,
				                .module = *vert_shader,
				                .pName = "main",
				                .pSpecializationInfo = &vert_sp_info,
				        },
				        vk::PipelineShaderStageCreateInfo{
				                .stage = vk::ShaderStageFlagBits::eFragment,
				                .module = *frag_shader[chroma_config],
				                .pName = "main",
				                .pSpecializationInfo = &frag_sp_info,
				        },
				};

				vk::PipelineVertexInputStateCreateInfo vert_info{};
				vk::PipelineInputAssemblyStateCreateInfo assembly_info{
				        .topology = vk::PrimitiveTopology::eTriangleList,
				};
				vk::PipelineViewportStateCreateInfo viewport_info{
				        .viewportCount = 1,
				        .scissorCount = 1,
				};
				vk::PipelineRasterizationStateCreateInfo rasterization_info{
				        .lineWidth = 1,
				};
				vk::PipelineMultisampleStateCreateInfo multisample_info{};
				std::array cb_attachments{
				        vk::PipelineColorBlendAttachmentState{
				                .colorWriteMask = vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags,
				        },
				        vk::PipelineColorBlendAttachmentState{
				                .colorWriteMask = vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags,
				        },
				        vk::PipelineColorBlendAttachmentState{
				                .colorWriteMask = vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags,
				        },
				};
				vk::PipelineColorBlendStateCreateInfo colorblend_info{
				        .attachmentCount = colorAttachmentCount,
				        .pAttachments = cb_attachments.data(),
				};
				std::array dynamic_states{
				        vk::DynamicState::eViewport,
				        vk::DynamicState::eScissor,
				};
				vk::PipelineDynamicStateCreateInfo dynamic_info{
				        .dynamicStateCount = dynamic_states.size(),
				        .pDynamicStates = dynamic_states.data(),
				};

				vk::GraphicsPipelineCreateInfo pipeline_info{
				        .stageCount = stages.size(),
				        .pStages = stages.data(),
				        .pVertexInputState = &vert_info,
				        .pInputAssemblyState = &assembly_info,
				        .pViewportState = &viewport_info,
				        .pRasterizationState = &rasterization_info,
				        .pMultisampleState = &multisample_info,
				        .pColorBlendState = &colorblend_info,
				        .pDynamicState = &dynamic_info,
				        .layout = std::get<1>(key),
				        .renderPass = std::get<0>(key),
				};
				it = fragment.pipelines.emplace(
				                               key,
				                               device.createGraphicsPipeline(nullptr, pipeline_info))
				             .first;
			}
			return std::make_tuple(std::get<0>(key), std::get<1>(key), *it->second);
		};

		for (int input_level = DecompositionLevels - 1; input_level >= 0; input_level--)
		{
			auto & in_level = fragment.levels[input_level];
			int output_level = input_level - 1;
			bool has_chroma_output = output_level >= 0 || chroma == ChromaSubsampling::Chroma444;

			// Vertical passes.
			for (int vert_pass = 0; vert_pass < 2; vert_pass++)
			{
				auto & pipeline = in_level.vertical[vert_pass];
				key_render_pass render_pass_key{};
				auto & formats = std::get<0>(render_pass_key);
				formats[0] = in_level.vert[vert_pass][0].info().format;
				if (has_chroma_output)
					formats[1] = in_level.vert[vert_pass][1].info().format;
				for (int32_t edge = -1; edge <= 1; ++edge)
				{
					std::tie(pipeline.rp, pipeline.layout, pipeline.pipeline[edge + 1]) = ensure_pipeline(
					        render_pass_key,
					        has_chroma_output ? 1 : 0,
					        sp{true, false, false, edge});
				}

				pipeline.ds = allocate_descriptor_set(*fragment.ds_layout[has_chroma_output ? 1 : 0]);

				std::vector image_info{
				        vk::DescriptorImageInfo{
				                .imageView = *in_level.decoded[0][vert_pass + 0],
				                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				        },
				        vk::DescriptorImageInfo{
				                .imageView = *in_level.decoded[0][vert_pass + 2],
				                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				        },
				        vk::DescriptorImageInfo{
				                .sampler = *mirror_repeat_sampler,
				        },
				};
				if (has_chroma_output)
				{
					image_info.push_back(vk::DescriptorImageInfo{
					        .imageView = *in_level.decoded[1][vert_pass + 0],
					        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
					});
					image_info.push_back(vk::DescriptorImageInfo{
					        .imageView = *in_level.decoded[1][vert_pass + 2],
					        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
					});
					image_info.push_back(vk::DescriptorImageInfo{
					        .imageView = *in_level.decoded[2][vert_pass + 0],
					        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
					});
					image_info.push_back(vk::DescriptorImageInfo{
					        .imageView = *in_level.decoded[2][vert_pass + 2],
					        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
					});
				}
				std::vector descriptor_writes{
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 0,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .pImageInfo = &image_info[0],
				        },
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 1,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .pImageInfo = &image_info[1],
				        },
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 2,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampler,
				                .pImageInfo = &image_info[2],
				        },
				};
				if (has_chroma_output)
				{
					for (uint32_t i = 3; i < 7; ++i)
						descriptor_writes.push_back(
						        vk::WriteDescriptorSet{
						                .dstSet = pipeline.ds,
						                .dstBinding = i,
						                .descriptorCount = 1,
						                .descriptorType = vk::DescriptorType::eSampledImage,
						                .pImageInfo = &image_info[i],
						        });
				}
				device.updateDescriptorSets(descriptor_writes, {});

				std::array fb_att{
				        *in_level.vert_views[vert_pass][0],
				        *in_level.vert_views[vert_pass][1],
				};

				vk::FramebufferCreateInfo fb_info{
				        .renderPass = pipeline.rp,
				        .attachmentCount = has_chroma_output ? 2u : 1u,
				        .pAttachments = fb_att.data(),
				        .width = in_level.vert[vert_pass][0].info().extent.width,
				        .height = in_level.vert[vert_pass][0].info().extent.height,
				        .layers = 1,
				};

				pipeline.fb_extent = vk::Extent2D{.width = fb_info.width, .height = fb_info.height};
				pipeline.fb = device.createFramebuffer(fb_info);
			}

			// Horizontal pass
			{
				auto & pipeline = in_level.horizontal;
				key_render_pass render_pass_key{};
				auto & formats = std::get<0>(render_pass_key);

				for (uint32_t comp = 0; comp < (has_chroma_output ? 3 : 1); comp++)
				{
					if (output_level < 0 || (output_level == 0 && chroma == ChromaSubsampling::Chroma420 && comp != 0))
						formats[comp] = vk::Format::eR8Unorm; // FIXME: check
					else
						formats[comp] = fragment.levels[output_level].horiz[comp].info().format;
				}

				for (int32_t edge = -1; edge <= 1; ++edge)
				{
					std::tie(pipeline.rp, pipeline.layout, pipeline.pipeline[edge + 1]) = ensure_pipeline(
					        render_pass_key,
					        has_chroma_output ? 2 : 0,
					        sp{false, output_level < 0, output_level < 0 || (output_level == 0 && chroma == ChromaSubsampling::Chroma420), edge});
				}

				pipeline.ds = allocate_descriptor_set(*fragment.ds_layout[has_chroma_output ? 2 : 0]);

				std::vector image_info{
				        vk::DescriptorImageInfo{
				                .imageView = *fragment.levels[input_level].vert_views[0][0],
				                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				        },
				        vk::DescriptorImageInfo{
				                .imageView = *fragment.levels[input_level].vert_views[1][0],
				                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				        },
				        vk::DescriptorImageInfo{
				                .sampler = *mirror_repeat_sampler,
				        },
				};
				if (has_chroma_output)
				{
					image_info.push_back(vk::DescriptorImageInfo{
					        .imageView = *fragment.levels[input_level].vert_views[0][1],
					        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
					});
					image_info.push_back(vk::DescriptorImageInfo{
					        .imageView = *fragment.levels[input_level].vert_views[1][1],
					        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
					});
				}
				std::vector descriptor_writes{
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 0,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .pImageInfo = &image_info[0],
				        },
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 1,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .pImageInfo = &image_info[1],
				        },
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 2,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampler,
				                .pImageInfo = &image_info[2],
				        },
				};
				if (has_chroma_output)
				{
					for (uint32_t i = 3; i < 5; ++i)
						descriptor_writes.push_back(
						        vk::WriteDescriptorSet{
						                .dstSet = pipeline.ds,
						                .dstBinding = i,
						                .descriptorCount = 1,
						                .descriptorType = vk::DescriptorType::eSampledImage,
						                .pImageInfo = &image_info[i],
						        });
				}
				device.updateDescriptorSets(descriptor_writes, {});

				// Framebuffers for output will be created for the specific views
				if (output_level > 0 or (output_level == 0 and chroma == ChromaSubsampling::Chroma444))
				{
					auto imgs = fragment.levels[output_level].horiz;
					auto views = fragment.levels[output_level].horiz_views;
					std::array<vk::ImageView, NumComponents> fb_att{
					        *views[0],
					        *views[1],
					        *views[2],
					};

					vk::FramebufferCreateInfo fb_info{
					        .renderPass = pipeline.rp,
					        .attachmentCount = has_chroma_output ? NumComponents : 1u,
					        .pAttachments = fb_att.data(),
					        .width = imgs[0].info().extent.width,
					        .height = imgs[0].info().extent.height,
					        .layers = 1,
					};

					pipeline.fb_extent = vk::Extent2D{.width = fb_info.width, .height = fb_info.height};
					pipeline.fb = device.createFramebuffer(fb_info);
				}
			}

			// special pipeline for fixup
			if (output_level == 0 && chroma == ChromaSubsampling::Chroma420)
			{
				auto & img = fragment.levels[output_level].horiz[0];
				auto & pipeline = fragment.level0_420;
				key_render_pass render_pass_key{};
				std::get<0>(render_pass_key)[0] = img.info().format;
				std::get<1>(render_pass_key)[0] = vk::ImageLayout::eColorAttachmentOptimal;
				pipeline.ds = allocate_descriptor_set(*fragment.ds_layout[0]);

				std::array image_info{
				        vk::DescriptorImageInfo{
				                .imageView = *fragment.levels[input_level].vert_views[0][0],
				                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				        },
				        vk::DescriptorImageInfo{
				                .imageView = *fragment.levels[input_level].vert_views[1][0],
				                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
				        },
				        vk::DescriptorImageInfo{
				                .sampler = *mirror_repeat_sampler,
				        },
				};
				std::array descriptor_writes{
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 0,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .pImageInfo = &image_info[0],
				        },
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 1,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampledImage,
				                .pImageInfo = &image_info[1],
				        },
				        vk::WriteDescriptorSet{
				                .dstSet = pipeline.ds,
				                .dstBinding = 2,
				                .descriptorCount = 1,
				                .descriptorType = vk::DescriptorType::eSampler,
				                .pImageInfo = &image_info[2],
				        },
				};
				device.updateDescriptorSets(descriptor_writes, {});
				std::tie(pipeline.rp, pipeline.layout, pipeline.pipeline[2]) = ensure_pipeline(render_pass_key, 0, sp{false, false, false, 1});

				vk::ImageView view = *fragment.levels[output_level].horiz_views[0];

				vk::FramebufferCreateInfo fb_info{
				        .renderPass = pipeline.rp,
				        .attachmentCount = 1,
				        .pAttachments = &view,
				        .width = img.info().extent.width,
				        .height = img.info().extent.height,
				        .layers = 1,
				};

				pipeline.fb_extent = vk::Extent2D{.width = fb_info.width, .height = fb_info.height};
				pipeline.fb = device.createFramebuffer(fb_info);
			}
		}
	}
	else
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

		auto shader = load_shader(device, std::string("idwt_" XSTR(PYROWAVE_PRECISION)) + (feat12.shaderFloat16 ? "_fp16" : ""));
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
				idwt_.ds[c][input_level] = allocate_descriptor_set(*idwt_.ds_layout);

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

	std::array image_barrier{
	        vk::ImageMemoryBarrier{
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
	        },
	        vk::ImageMemoryBarrier{
	                .srcAccessMask = vk::AccessFlagBits::eNone,
	                .dstAccessMask = vk::AccessFlagBits::eShaderWrite,
	                .oldLayout = vk::ImageLayout::eUndefined,
	                .newLayout = vk::ImageLayout::eGeneral,
	                .image = wavelet_img_low_res,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .levelCount = VK_REMAINING_MIP_LEVELS,
	                        .layerCount = VK_REMAINING_ARRAY_LAYERS,
	                },
	        },
	};

	if (wavelet_img_low_res)
	{
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eComputeShader,
		        vk::PipelineStageFlagBits::eComputeShader,
		        {},
		        {},
		        {},
		        image_barrier);
	}
	else
	{
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eComputeShader,
		        vk::PipelineStageFlagBits::eComputeShader,
		        {},
		        {},
		        {},
		        image_barrier[0]);
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
				push.resolution.y = get_height(wavelet_img_high_res, level);
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

	if (fragment_path)
	{
		for (auto & b: image_barrier)
		{
			b.srcAccessMask = b.dstAccessMask;
			b.oldLayout = b.newLayout;
			b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			b.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		}
		if (wavelet_img_low_res)
		{
			cmd.pipelineBarrier(
			        vk::PipelineStageFlagBits::eComputeShader,
			        vk::PipelineStageFlagBits::eFragmentShader,
			        {},
			        {},
			        {},
			        image_barrier);
		}
		else
		{
			cmd.pipelineBarrier(
			        vk::PipelineStageFlagBits::eComputeShader,
			        vk::PipelineStageFlagBits::eFragmentShader,
			        {},
			        {},
			        {},
			        image_barrier[0]);
		}
	}
	else
	{
		vk::MemoryBarrier barrier{
		        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
		        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
		};
		cmd.pipelineBarrier(
		        vk::PipelineStageFlagBits::eComputeShader,
		        fragment_path ? vk::PipelineStageFlagBits::eFragmentShader : vk::PipelineStageFlagBits::eComputeShader,
		        {},
		        barrier,
		        {},
		        {});
	}

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

bool Decoder::idwt_fragment(vk::raii::CommandBuffer & cmd, const ViewBuffers & views)
{
	std::vector<vk::ImageMemoryBarrier> barriers;
	{
		const auto add_discard = [&](image_allocation & img) {
			if (img)
			{
				barriers.push_back(vk::ImageMemoryBarrier{
				        .srcAccessMask = vk::AccessFlagBits::eNone,
				        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
				        .oldLayout = vk::ImageLayout::eUndefined,
				        .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
				        .image = img,
				        .subresourceRange = {
				                .aspectMask = vk::ImageAspectFlagBits::eColor,
				                .levelCount = vk::RemainingMipLevels,
				                .layerCount = vk::RemainingArrayLayers,
				        },
				});
			}
		};
		for (auto & level: fragment.levels)
		{
			for (auto & vert: level.vert)
				for (auto & comp: vert)
					add_discard(comp);
			for (auto & comp: level.horiz)
				add_discard(comp);
		}
		if (not barriers.empty())
		{
			cmd.pipelineBarrier(
			        vk::PipelineStageFlagBits::eFragmentShader,
			        vk::PipelineStageFlagBits::eColorAttachmentOutput,
			        {},
			        {},
			        {},
			        barriers);
			barriers.clear();
		}
	}

	const auto add_read_only = [&](image_allocation & img) {
		if (img)
		{
			barriers.push_back(vk::ImageMemoryBarrier{
			        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
			        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
			        .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
			        .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
			        .image = img,
			        .subresourceRange = {
			                .aspectMask = vk::ImageAspectFlagBits::eColor,
			                .levelCount = vk::RemainingMipLevels,
			                .layerCount = vk::RemainingArrayLayers,
			        },
			});
		}
	};
	// auto start_idwt = cmd.write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	for (int input_level = DecompositionLevels - 1; input_level >= 0; input_level--)
	{
		int output_level = input_level - 1;

		if (output_level >= 0)
			begin_label(cmd, std::format("Fragment iDWT level {}", output_level).c_str());
		else
			begin_label(cmd, "Fragment iDWT final");

		bool has_chroma_output = output_level >= 0 || chroma == ChromaSubsampling::Chroma444;
		// Vertical passes.
		for (int vert_pass = 0; vert_pass < 2; vert_pass++)
		{
			const auto & pipeline = fragment.levels[input_level].vertical[vert_pass];
			cmd.beginRenderPass(vk::RenderPassBeginInfo{
			                            .renderPass = pipeline.rp,
			                            .framebuffer = *pipeline.fb,
			                            .renderArea = {.extent = pipeline.fb_extent},
			                    },
			                    {});
			cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.layout, 0, pipeline.ds, {});

			const auto & extent = fragment.levels[input_level].vert[vert_pass][0].info().extent;

			// Set mirror point.
			// Work around broken Mali r38.1 compiler.
			// If it sees negative texture offsets it breaks the output for whatever reason (!?!?!?!).
			const auto & input_dim = fragment.levels[input_level].decoded_dim;
			IDwtFragmentPushData push{
			        .uv_offset = glm::vec2(0, -2.f / input_dim.height),
			        .half_texel_offset = 0.5f / glm::vec2(input_dim.width, input_dim.height),
			        .vp_scale = float(pipeline.fb_extent.height),
			        .pivot_size = pipeline.fb_extent.height,
			};
			cmd.pushConstants<IDwtFragmentPushData>(pipeline.layout, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex, 0, push);
			cmd.setViewport(0, vk::Viewport{
			                           .width = float(pipeline.fb_extent.width),
			                           .height = float(pipeline.fb_extent.height),
			                           .maxDepth = 1,
			                   });

			// Render top edge condition.
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline[0]);
			cmd.setScissor(0, vk::Rect2D{.extent = {.width = pipeline.fb_extent.width, .height = 8}});
			cmd.draw(3, 1, 0, 0);

			// Render normal path
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline[1]);
			cmd.setScissor(0, vk::Rect2D{.offset = {.x = 0, .y = 8}, .extent = {.width = pipeline.fb_extent.width, .height = pipeline.fb_extent.height - 16}});
			cmd.draw(3, 1, 0, 0);

			// Render bottom edge condition
			cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline[2]);
			cmd.setScissor(0, vk::Rect2D{.offset = {.x = 0, .y = int32_t(pipeline.fb_extent.height - 8)}, .extent = {.width = pipeline.fb_extent.width, .height = 8}});
			cmd.draw(3, 1, 0, 0);

			cmd.endRenderPass();
		}

		assert(barriers.empty());
		for (auto & vert: fragment.levels[input_level].vert)
			for (auto & comp: vert)
				add_read_only(comp);
		if (not barriers.empty())
		{
			cmd.pipelineBarrier(
			        vk::PipelineStageFlagBits::eColorAttachmentOutput,
			        vk::PipelineStageFlagBits::eFragmentShader,
			        {},
			        {},
			        {},
			        barriers);
			barriers.clear();
		}

		const auto & pipeline = fragment.levels[input_level].horizontal;
		vk::RenderPassBeginInfo begin_info{
		        .renderPass = pipeline.rp,
		        .framebuffer = *pipeline.fb,
		        .renderArea = {.extent = pipeline.fb_extent},
		};
		if (output_level == 0 and chroma == ChromaSubsampling::Chroma420)
		{
			begin_info.renderArea.extent = vk::Extent2D{uint32_t(width / 2), uint32_t(height / 2)};

			auto it = fragment.framebuffers.find(views[1]);
			if (it == fragment.framebuffers.end())
			{
				auto v = fragment.levels[output_level].horiz_views;
				std::array<vk::ImageView, NumComponents> fb_att{
				        *v[0],
				        views[1],
				        views[2],
				};

				vk::FramebufferCreateInfo fb_info{
				        .renderPass = pipeline.rp,
				        .attachmentCount = fb_att.size(),
				        .pAttachments = fb_att.data(),
				        .width = uint32_t(width / 2),
				        .height = uint32_t(height / 2),
				        .layers = 1,
				};

				it = fragment.framebuffers.emplace(views[1], device.createFramebuffer(fb_info)).first;
			}
			begin_info.framebuffer = *it->second;
		}
		else if (output_level == -1)
		{
			begin_info.renderArea.extent = vk::Extent2D{uint32_t(width), uint32_t(height)};
			auto it = fragment.framebuffers.find(views[0]);
			if (it == fragment.framebuffers.end())
			{
				std::array<vk::ImageView, NumComponents> fb_att{
				        views[0],
				        views[1],
				        views[2],
				};

				vk::FramebufferCreateInfo fb_info{
				        .renderPass = pipeline.rp,
				        .attachmentCount = chroma == ChromaSubsampling::Chroma420 ? 1u : 3u,
				        .pAttachments = fb_att.data(),
				        .width = uint32_t(width),
				        .height = uint32_t(height),
				        .layers = 1,
				};

				it = fragment.framebuffers.emplace(views[0], device.createFramebuffer(fb_info)).first;
			}
			begin_info.framebuffer = *it->second;
		}
		cmd.beginRenderPass(begin_info, {});
		cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.layout, 0, pipeline.ds, {});

		uint32_t aligned_render_width = aligned_width >> (output_level + 1);
		uint32_t aligned_render_height = aligned_height >> (output_level + 1);

		// Chroma output might be smaller than Y in output_level == 0 due to not using alignment.
		// This is reflected in the actual render area, which is equal to default viewport.
		auto render_width = begin_info.renderArea.extent.width;
		auto render_height = begin_info.renderArea.extent.height;

		// In case we're rendering to an output texture,
		// the render area might be smaller than we expect for purposes of alignment.
		// Use properly scaled viewport that we scissor away as needed.
		cmd.setViewport(0, vk::Viewport{
		                           .width = float(aligned_render_width),
		                           .height = float(aligned_render_height),
		                           .maxDepth = 1,
		                   });

		// Set mirror point.
		auto & input_extent = fragment.levels[input_level].vert[0][0].info().extent;
		IDwtFragmentPushData push{
		        .uv_offset = glm::vec2(-2.f / input_extent.width, 0),
		        .half_texel_offset = 0.5f / glm::vec2(input_extent.width, input_extent.height),
		        .vp_scale = float(begin_info.renderArea.extent.width),
		        .pivot_size = aligned_render_width,
		};
		cmd.pushConstants<IDwtFragmentPushData>(pipeline.layout, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex, 0, push);

		// Render left edge condition.
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline[0]);
		cmd.setScissor(0, vk::Rect2D{.extent = {.width = 8, .height = render_height}});
		cmd.draw(3, 1, 0, 0);

		// Render normal condition
		cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline[1]);
		cmd.setScissor(0, vk::Rect2D{
		                          .offset = {.x = 8, .y = 0},
		                          .extent = {.width = std::min<uint32_t>(render_width - 8, aligned_render_width - 16), .height = render_height},
		                  });
		cmd.draw(3, 1, 0, 0);

		uint32_t aligned_x = aligned_render_width - 8;
		if (aligned_x < render_width)
		{
			// Render right edge condition
			cmd.setScissor(0, vk::Rect2D{
			                          .offset = {.x = int32_t(aligned_x), .y = 0},
			                          .extent = {.width = render_width - aligned_x, .height = render_height},
			                  });
			cmd.draw(3, 1, 0, 0);
		}

		cmd.endRenderPass();

		// If chroma is subsampled, we cannot render the fully padded region in one render pass due to
		// rules regarding renderArea. renderArea cannot exceed the smallest image in the render pass.
		// We cannot use subpasses either, so split the render pass, but that's mostly fine,
		// since renderArea is non-overlapping.
		if (output_level == 0 && chroma == ChromaSubsampling::Chroma420)
		{
			vk::MemoryBarrier2 by_region{
			        .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			        .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
			        .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
			        .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
			};
			vk::DependencyInfo dep{
			        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
			        .memoryBarrierCount = 1,
			        .pMemoryBarriers = &by_region,
			};

			// Need vertical fixup (very common for 1080p).
			auto & pipeline = fragment.level0_420;
			auto extent = pipeline.fb_extent;
			if (render_height < extent.height)
			{
				// Insert a simple by_region barrier to ensure we follow Vulkan rules for RW access.
				cmd.pipelineBarrier2(dep);

				cmd.beginRenderPass(vk::RenderPassBeginInfo{
				                            .renderPass = pipeline.rp,
				                            .framebuffer = *pipeline.fb,
				                            .renderArea = {
				                                    .offset = {0, int32_t(render_height)},
				                                    .extent = {extent.width, extent.height - render_height},
				                            },
				                    },
				                    {});

				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline[2]);
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.layout, 0, pipeline.ds, {});
				cmd.setViewport(0, vk::Viewport{
				                           .width = float(aligned_render_width),
				                           .height = float(aligned_render_height),
				                           .maxDepth = 1,
				                   });
				cmd.setScissor(0, vk::Rect2D{
				                          .offset = {0, int32_t(render_height)},
				                          .extent = {extent.width, extent.height - render_height},
				                  });
				cmd.pushConstants<IDwtFragmentPushData>(pipeline.layout, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex, 0, push);

				cmd.draw(3, 1, 0, 0);
				cmd.endRenderPass();
			}

			// Need horizontal fixup (very rare).
			if (render_width < extent.width)
			{
				cmd.pipelineBarrier2(dep);

				cmd.beginRenderPass(vk::RenderPassBeginInfo{
				                            .renderPass = pipeline.rp,
				                            .framebuffer = *pipeline.fb,
				                            .renderArea = {
				                                    .offset = {int32_t(render_width), 0},
				                                    .extent = {extent.width - render_width, extent.height},
				                            },
				                    },
				                    {});
				cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.pipeline[2]);
				cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline.layout, 0, pipeline.ds, {});
				cmd.pushConstants<IDwtFragmentPushData>(pipeline.layout, vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex, 0, push);
				cmd.setViewport(0, vk::Viewport{
				                           .width = float(aligned_render_width),
				                           .height = float(aligned_render_height),
				                           .maxDepth = 1,
				                   });
				cmd.setScissor(0, vk::Rect2D{
				                          .offset = {int32_t(render_width), 0},
				                          .extent = {extent.width - render_width, extent.height},
				                  });
				cmd.draw(3, 1, 0, 0);
				cmd.endRenderPass();
			}
		}

		if (output_level >= 0)
		{
			assert(barriers.empty());
			for (auto & comp: fragment.levels[output_level].horiz)
				add_read_only(comp);
			if (not barriers.empty())
			{
				cmd.pipelineBarrier(
				        vk::PipelineStageFlagBits::eColorAttachmentOutput,
				        vk::PipelineStageFlagBits::eFragmentShader,
				        {},
				        {},
				        {},
				        barriers);
				barriers.clear();
			}
		}

		end_label(cmd);
	}

	// auto end_idwt = cmd.write_timestamp(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
	// device->register_time_interval("GPU", std::move(start_idwt), std::move(end_idwt), "iDWT fragment");

	// Avoid WAR hazard for dequantization.
	vk::MemoryBarrier barrier{
	        .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
	        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
	};
	cmd.pipelineBarrier(
	        vk::PipelineStageFlagBits::eFragmentShader,
	        vk::PipelineStageFlagBits::eComputeShader,
	        {},
	        barrier,
	        {},
	        {});

	return true;
}

bool Decoder::decode(vk::raii::CommandBuffer & cmd, DecoderInput & input, const ViewBuffers & views)
{
	for (int level = 0; level < DecompositionLevels; level++)
	{
		for (int component = 0; component < NumComponents; component++)
		{
			if (use_readonly_texel_buffer)
			{
				std::array buffer_info{
				        vk::DescriptorBufferInfo{
				                .buffer = input.dequant_offset_buffer,
				                .range = vk::WholeSize,
				        },
				};

				device.updateDescriptorSets(
				        std::array{
				                vk::WriteDescriptorSet{
				                        .dstSet = dequant_.ds[component][level],
				                        .dstBinding = 1,
				                        .descriptorCount = buffer_info.size(),
				                        .descriptorType = vk::DescriptorType::eStorageBuffer,
				                        .pBufferInfo = buffer_info.data(),
				                },
				                vk::WriteDescriptorSet{
				                        .dstSet = dequant_.ds[component][level],
				                        .dstBinding = 2,
				                        .descriptorCount = 1,
				                        .descriptorType = vk::DescriptorType::eUniformTexelBuffer,
				                        .pTexelBufferView = &*input.u32_view,
				                },
				                vk::WriteDescriptorSet{
				                        .dstSet = dequant_.ds[component][level],
				                        .dstBinding = 3,
				                        .descriptorCount = 1,
				                        .descriptorType = vk::DescriptorType::eUniformTexelBuffer,
				                        .pTexelBufferView = &*input.u16_view,
				                },
				                vk::WriteDescriptorSet{
				                        .dstSet = dequant_.ds[component][level],
				                        .dstBinding = 4,
				                        .descriptorCount = 1,
				                        .descriptorType = vk::DescriptorType::eUniformTexelBuffer,
				                        .pTexelBufferView = &*input.u8_view,
				                },
				        },
				        {});
			}
			else
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

	if (fragment_path)
	{
		if (not idwt_fragment(cmd, views))
			return false;
	}
	else
	{
		if (!idwt(cmd, views))
			return false;
	}

	return true;
}

vk::DescriptorSet Decoder::allocate_descriptor_set(vk::DescriptorSetLayout layout)
{
	return device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{
	        .descriptorPool = *ds_pool,
	        .descriptorSetCount = 1,
	        .pSetLayouts = &layout,
	})[0]
	        .release();
}

} // namespace PyroWave
