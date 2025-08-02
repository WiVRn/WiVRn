// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

#include "pyrowave_common.h"

namespace PyroWave
{
class Decoder : public WaveletBuffers
{
	vk::raii::DescriptorPool ds_pool;
	struct pipeline
	{
		vk::raii::DescriptorSetLayout ds_layout = nullptr;
		vk::DescriptorSet ds[NumComponents][DecompositionLevels];
		vk::raii::PipelineLayout layout = nullptr;
		vk::raii::Pipeline pipeline = nullptr;
	};

	buffer_allocation dequant_offset_buffer, payload_data, payload_staging, dequant_staging;

	pipeline dequant_;
	pipeline idwt_;
	vk::raii::Pipeline idwt_dcshift = nullptr;

	std::vector<uint32_t> dequant_offset_buffer_cpu;
	std::vector<uint32_t> payload_data_cpu;
	int decoded_blocks = 0;
	int total_blocks_in_sequence = 0;
	uint32_t last_seq = UINT32_MAX;
	bool decoded_frame_for_current_sequence = false;

public:
	using ViewBuffers = std::array<vk::ImageView, 3>;

	Decoder(vk::raii::PhysicalDevice & phys_dev, vk::raii::Device & device, int width, int height, ChromaSubsampling chroma);
	~Decoder();

	void clear();
	bool push_packet(const void * data, size_t size);
	bool decode(vk::raii::CommandBuffer & cmd, const ViewBuffers & views);
	bool decode_is_ready(bool allow_partial_frame) const;

private:
	bool decode_packet(const BitstreamHeader * header);

	bool dequant(vk::raii::CommandBuffer & cmd);
	bool idwt(vk::raii::CommandBuffer & cmd, const ViewBuffers & views);

	void upload_payload(vk::raii::CommandBuffer & cmd);
};
} // namespace PyroWave
