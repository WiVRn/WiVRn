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

#include <deque>
#include <memory>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace wivrn
{

class video_encoder_vulkan_h265 : public video_encoder_vulkan
{
	std::deque<int32_t> poc_history;
	bool sample_adaptive_offset_enabled = false;

	StdVideoH265SequenceParameterSetVui vui{};
	StdVideoH265DecPicBufMgr dpb{};

	StdVideoH265ProfileTierLevel ptl{};
	StdVideoH265VideoParameterSet vps{};

	StdVideoH265SequenceParameterSet sps{};

	StdVideoH265PictureParameterSet pps{};

	StdVideoH265ShortTermRefPicSet st_rps{};

	StdVideoEncodeH265SliceSegmentHeader slice_header{};
	vk::VideoEncodeH265NaluSliceSegmentInfoKHR nalu_slice_info{};

	StdVideoEncodeH265ReferenceListsInfo reference_lists_info{};
	StdVideoEncodeH265PictureInfo std_picture_info{};
	vk::VideoEncodeH265PictureInfoKHR picture_info{};

	std::vector<StdVideoEncodeH265ReferenceInfo> dpb_std_info;
	std::vector<vk::VideoEncodeH265DpbSlotInfoKHR> dpb_std_slots;

	vk::VideoEncodeH265GopRemainingFrameInfoKHR gop_info{};
	vk::VideoEncodeH265RateControlInfoKHR rc_h265{};
	vk::VideoEncodeH265RateControlLayerInfoKHR rc_layer_h265{};

	video_encoder_vulkan_h265(wivrn_vk_bundle & vk,
	                          const vk::VideoCapabilitiesKHR & video_caps,
	                          const vk::VideoEncodeCapabilitiesKHR & encode_caps,
	                          uint8_t stream_idx,
	                          const encoder_settings & settings);

protected:
	std::vector<void *> setup_slot_info(size_t dpb_size) override;

	void * encode_info_next(uint32_t frame_num, size_t slot, std::optional<int32_t> ref_slot) override;
	virtual vk::ExtensionProperties std_header_version() override;

	void send_idr_data() override;

public:
	static std::unique_ptr<video_encoder_vulkan_h265> create(wivrn_vk_bundle & vk,
	                                                         const encoder_settings & settings,
	                                                         uint8_t stream_idx);

	std::vector<uint8_t> get_vps_sps_pps();

	static const vk::StructureChain<vk::VideoProfileInfoKHR, vk::VideoEncodeH265ProfileInfoKHR, vk::VideoEncodeUsageInfoKHR> video_profile_info;
};
} // namespace wivrn
