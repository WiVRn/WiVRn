// Copyright (c) 2025 Hans-Kristian Arntzen
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_raii.hpp>

#include "pyrowave_common.h"

namespace PyroWave
{

class Decoder;

class DecoderInput
{
	friend class Decoder;

	const Decoder & decoder;

	buffer_allocation dequant_offset_buffer;
	buffer_allocation dequant_staging;
	std::span<uint32_t> dequant_data;
	buffer_allocation payload_data;
	buffer_allocation payload_staging;
	uint8_t * payload = nullptr;
	size_t payload_size = 0;

	BitstreamHeader header{};
	size_t header_size = 0;
	size_t packet_size = 0;

	int decoded_blocks = 0;
	uint32_t last_seq = UINT32_MAX;
	int total_blocks_in_sequence = 0;

public:
	DecoderInput(const Decoder &);
	bool push_data(std::span<const uint8_t> data);
	void clear();

private:
	void push_raw(const void * data, size_t size);
};

class Decoder : public WaveletBuffers
{
	friend class DecoderInput;

	vk::raii::DescriptorPool ds_pool;
	struct pipeline
	{
		vk::raii::DescriptorSetLayout ds_layout = nullptr;
		vk::DescriptorSet ds[NumComponents][DecompositionLevels];
		vk::raii::PipelineLayout layout = nullptr;
		vk::raii::Pipeline pipeline = nullptr;
	};

	pipeline dequant_;
	pipeline idwt_;
	vk::raii::Pipeline idwt_dcshift = nullptr;

public:
	using ViewBuffers = std::array<vk::ImageView, 3>;

	Decoder(vk::raii::PhysicalDevice & phys_dev, vk::raii::Device & device, int width, int height, ChromaSubsampling chroma);
	~Decoder();

	bool decode(vk::raii::CommandBuffer & cmd, DecoderInput & input, const ViewBuffers & views);

private:
	bool dequant(vk::raii::CommandBuffer & cmd);
	bool idwt(vk::raii::CommandBuffer & cmd, const ViewBuffers & views);
};
} // namespace PyroWave
