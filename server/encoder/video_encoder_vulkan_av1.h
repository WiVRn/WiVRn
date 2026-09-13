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

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <vulkan/vulkan.hpp>

#include <vk_video/vulkan_video_codec_av1std.h>
#include <vk_video/vulkan_video_codec_av1std_encode.h>

namespace wivrn
{
class video_encoder_vulkan_av1 : public video_encoder_vulkan
{
	StdVideoAV1ColorConfig color_config{};
	StdVideoAV1SequenceHeader seq_header{};

	StdVideoAV1TileInfo tile_info{};
	StdVideoAV1Quantization quantization{};
	StdVideoAV1LoopFilter loop_filter{};
	StdVideoAV1CDEF cdef{};
	StdVideoAV1LoopRestoration loop_restoration{};
	StdVideoAV1GlobalMotion global_motion{};
	StdVideoAV1Segmentation segmentation{};

	StdVideoEncodeAV1PictureInfo std_picture_info{};
	StdVideoEncodeAV1OperatingPointInfo operating_point{};
	std::array<int32_t, VK_MAX_VIDEO_AV1_REFERENCES_PER_FRAME_KHR> reference_name_slot_indices{};
	std::array<uint16_t, 2> mi_col_starts{};
	std::array<uint16_t, 2> mi_row_starts{};
	std::array<uint16_t, 1> tile_widths_sb{};
	std::array<uint16_t, 1> tile_heights_sb{};

	std::vector<StdVideoEncodeAV1ReferenceInfo> dpb_std_info;
	std::vector<vk::VideoEncodeAV1DpbSlotInfoKHR> dpb_std_slots;

	vk::VideoEncodeAV1PictureInfoKHR picture_info{};

	vk::VideoEncodeAV1GopRemainingFrameInfoKHR gop_info{};
	vk::VideoEncodeQualityLevelInfoKHR quality_level_info{};
	vk::VideoEncodeAV1RateControlInfoKHR rate_control_av1{};
	vk::VideoEncodeAV1RateControlLayerInfoKHR rate_control_layer_av1{};

	vk::VideoEncodeAV1StdFlagsKHR std_flags{};
	uint32_t single_reference_name_mask = 0;
	uint32_t max_single_reference_count = 0;
	uint32_t max_unidirectional_compound_reference_count = 0;
	uint32_t max_bidirectional_compound_reference_count = 0;
	uint8_t max_q_index = 0;
	uint8_t min_q_index = 0;
	uint32_t superblock_size = 64;
	uint8_t order_hint_bits = 7;

	video_encoder_vulkan_av1(wivrn::vk_bundle & vk,
	                         const vk::VideoCapabilitiesKHR & video_caps,
	                         const vk::VideoEncodeCapabilitiesKHR & encode_caps,
	                         const vk::VideoEncodeAV1CapabilitiesKHR & encode_av1_caps,
	                         uint8_t stream_idx,
	                         const encoder_settings & settings);

	void configure_from_caps(const vk::VideoEncodeAV1CapabilitiesKHR & encode_av1_caps);

protected:
	std::vector<void *> setup_slot_info(size_t dpb_size) override;

	void * encode_info_next(uint32_t frame_num, size_t slot, std::optional<int32_t> ref_slot) override;
	vk::ExtensionProperties std_header_version() override;

	void send_idr_data() override;

public:
	static std::unique_ptr<video_encoder_vulkan_av1> create(wivrn::vk_bundle & vk,
	                                                        const encoder_settings & settings,
	                                                        uint8_t stream_idx);
};
} // namespace wivrn
