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

#include "video_encoder_vulkan.h"

#include <memory>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace wivrn
{
class video_encoder_vulkan_h265 : public video_encoder_vulkan
{
	uint16_t idr_id = 0;
	StdVideoH265VideoParameterSet vps;
	StdVideoH265SequenceParameterSet sps;
	StdVideoH265PictureParameterSet pps;

	StdVideoEncodeH265SliceSegmentHeader slice_header;
	vk::VideoEncodeH265NaluSliceSegmentInfoKHR nalu_slice_info;

	StdVideoEncodeH265PictureInfo std_picture_info;
	vk::VideoEncodeH265PictureInfoKHR picture_info;

	StdVideoEncodeH265ReferenceListsInfo reference_lists_info;

	std::vector<StdVideoEncodeH265ReferenceInfo> dpb_std_info;
	std::vector<vk::VideoEncodeH265DpbSlotInfoKHR> dpb_std_slots;

	vk::VideoEncodeH265GopRemainingFrameInfoKHR gop_info;
	vk::VideoEncodeH265RateControlInfoKHR rate_control_h265;

	video_encoder_vulkan_h265(wivrn_vk_bundle & vk, vk::Rect2D rect, vk::VideoEncodeCapabilitiesKHR encode_caps, float fps, uint64_t bitrate);

protected:
	std::vector<void *> setup_slot_info(size_t dpb_size) override;

	void * encode_info_next(uint32_t frame_num, size_t slot, std::optional<size_t> ref) override;
	virtual vk::ExtensionProperties std_header_version() override;

	void send_idr_data() override;

public:
	static std::unique_ptr<video_encoder_vulkan_h265> create(wivrn_vk_bundle & vk,
	                                                         encoder_settings & settings,
	                                                         float fps);

	std::vector<uint8_t> get_vps_sps_pps();

	static const vk::StructureChain<vk::VideoProfileInfoKHR, vk::VideoEncodeH265ProfileInfoKHR, vk::VideoEncodeUsageInfoKHR> video_profile_info;
};
} // namespace wivrn
