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

#pragma once

#include "utils/sync_queue.h"
#include "vk/device_memory.h"
#include "vk/image.h"
#include "vk/renderpass.h"
#include "wivrn_packets.h"
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <vulkan/vulkan_core.h>

#define DEUGLIFY(x)                      \
	struct x##_deleter               \
	{                                \
		void operator()(x * ptr) \
		{                        \
			x##_delete(ptr); \
		}                        \
	};                               \
	using x##_ptr = std::unique_ptr<x, x##_deleter>;

DEUGLIFY(AImageReader)
DEUGLIFY(AMediaCodec)

class shard_accumulator;

namespace scenes
{
class stream;
}

namespace wivrn::android
{
class decoder
{
public:
	struct pipeline_context;

	struct blit_target
	{
		VkImage image;
		VkImageView image_view;
		VkOffset2D offset;
		VkExtent2D extent;

		VkFramebuffer framebuffer;
		std::shared_ptr<pipeline_context> pipeline;
	};

	struct mapped_hardware_buffer;

	struct blit_handle
	{
		xrt::drivers::wivrn::from_headset::feedback feedback;
		xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t view_info;

		std::shared_ptr<mapped_hardware_buffer> vk_data;

		AImage * aimage{};

		~blit_handle();
	};

private:
	xrt::drivers::wivrn::to_headset::video_stream_description::item description;
	float fps;

	VkDevice device;

	AImageReader_ptr image_reader;

	AMediaCodec_ptr media_codec;
	std::weak_ptr<scenes::stream> weak_scene;
	shard_accumulator * accumulator;

	PFN_vkGetAndroidHardwareBufferPropertiesANDROID vkGetAndroidHardwareBufferPropertiesANDROID;

	void on_image_available(AImageReader * reader);
	static void on_image_available(void * context, AImageReader * reader);

	utils::sync_queue<int32_t> input_buffers;
	utils::sync_queue<int32_t> output_buffers;
	utils::sync_queue<std::pair<xrt::drivers::wivrn::from_headset::feedback, xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t>> frame_infos;

	std::thread output_releaser;
	static void on_media_error(AMediaCodec *, void * userdata, media_status_t error, int32_t actionCode, const char * detail);
	static void on_media_format_changed(AMediaCodec *, void * userdata, AMediaFormat *);
	static void on_media_input_available(AMediaCodec *, void * userdata, int32_t index);
	static void on_media_output_available(AMediaCodec *, void * userdata, int32_t index, AMediaCodecBufferInfo * bufferInfo);

	void push_nals(std::span<std::span<const uint8_t>> data, int64_t timestamp, uint32_t flags);

	std::vector<blit_target> blit_targets;

	std::shared_ptr<pipeline_context> pipeline;
	std::mutex hbm_mutex;
	std::unordered_map<AHardwareBuffer *, std::shared_ptr<mapped_hardware_buffer>> hardware_buffer_map;
	vk::renderpass renderpass;

	std::shared_ptr<mapped_hardware_buffer> map_hardware_buffer(AImage *);

public:
	decoder(
	        VkDevice device,
	        VkPhysicalDevice physical_device,
	        const xrt::drivers::wivrn::to_headset::video_stream_description::item & description,
		float fps,
	        std::weak_ptr<scenes::stream> scene,
	        shard_accumulator * accumulator);

	decoder(const decoder &) = delete;
	decoder(decoder &&) = delete;
	~decoder();

	void push_data(std::span<std::span<const uint8_t>> data, uint64_t frame_index, bool partial);

	void frame_completed(xrt::drivers::wivrn::from_headset::feedback &, const xrt::drivers::wivrn::to_headset::video_stream_data_shard::view_info_t & view_info);

	const auto & desc() const
	{
		return description;
	}

	static const VkImageLayout framebuffer_expected_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	static const VkImageUsageFlagBits framebuffer_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	void set_blit_targets(std::vector<blit_target> targets, VkFormat format);
	void blit(VkCommandBuffer command_buffer, blit_handle & handle, std::span<int> target_indices);
	void blit_finished(blit_handle & handle);
};

} // namespace wivrn::android
