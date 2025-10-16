/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <unordered_map>
#include <vector>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "video_encoder.h"
#include "vk/allocation.h"

namespace wivrn
{
class video_encoder_vulkan : public video_encoder
{
	wivrn_vk_bundle & vk;
	const vk::VideoEncodeCapabilitiesKHR encode_caps;

	vk::raii::VideoSessionKHR video_session = nullptr;
	vk::raii::VideoSessionParametersKHR video_session_parameters = nullptr;

	vk::raii::QueryPool query_pool = nullptr;
	vk::raii::CommandPool transfer_command_pool = nullptr;
	vk::raii::CommandPool video_command_pool = nullptr;

	vk::ImageViewUsageCreateInfo image_view_template_next;
	vk::ImageViewCreateInfo image_view_template;
	std::unordered_map<VkImage, vk::raii::ImageView> image_views; // for input images
	struct slot_item
	{
		image_allocation tmp_image; // Used if we have an offset in the image to encode
		vk::raii::CommandBuffer video_cmd_buf = nullptr;
		vk::raii::CommandBuffer transfer_cmd_buf = nullptr;
		vk::raii::Semaphore wait_sem = nullptr;
		vk::raii::Semaphore sem = nullptr;
		vk::raii::Fence fence = nullptr;
		vk::raii::ImageView view = nullptr;
		buffer_allocation output_buffer;
		buffer_allocation host_buffer;
		bool idr = false;
	};
	std::array<slot_item, num_slots> slot_data;

	image_allocation dpb_image;

	std::vector<vk::raii::DeviceMemory> mem;

	vk::VideoFormatPropertiesKHR select_video_format(
	        vk::raii::PhysicalDevice & physical_device,
	        const vk::PhysicalDeviceVideoFormatInfoKHR &);

	bool session_initialized = false;
	const vk::Rect2D rect;

protected:
	const uint8_t num_dpb_slots;
	vk::Extent3D aligned_extent;
	vk::VideoEncodeRateControlLayerInfoKHR rate_control_layer;
	std::optional<vk::VideoEncodeRateControlInfoKHR> rate_control;

	video_encoder_vulkan(wivrn_vk_bundle & vk,
	                     vk::Rect2D rect,
	                     const vk::VideoCapabilitiesKHR & video_caps,
	                     const vk::VideoEncodeCapabilitiesKHR & encode_caps,
	                     float fps,
	                     uint8_t stream_idx,
	                     const encoder_settings & settings);

	void init(const vk::VideoCapabilitiesKHR & video_caps,
	          const vk::VideoProfileInfoKHR & video_profile,
	          void * video_session_create_next,
	          void * session_params_next);

	virtual ~video_encoder_vulkan();

	std::vector<uint8_t> get_encoded_parameters(void * next);

	virtual void send_idr_data() = 0;

	virtual std::vector<void *> setup_slot_info(size_t dpb_size) = 0;
	virtual void * encode_info_next(uint32_t frame_num, size_t slot, std::optional<int32_t> reference_slot) = 0;
	virtual vk::ExtensionProperties std_header_version() = 0;

public:
	std::optional<data> encode(uint8_t slot, uint64_t frame_index) override;
	std::pair<bool, vk::Semaphore> present_image(vk::Image y_cbcr, vk::raii::CommandBuffer & cmd_buf, uint8_t slot, uint64_t frame_index) override;
	void post_submit(uint8_t slot) override;
};
} // namespace wivrn
