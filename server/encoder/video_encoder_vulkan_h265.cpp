/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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
#include "video_encoder_vulkan_h265.h"

#include "encoder/encoder_settings.h"
#include "utils/wivrn_vk_bundle.h"
#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>

static StdVideoH265LevelIdc choose_level(uint32_t w, uint32_t h, float fps)
{
	const uint64_t pixrate = uint64_t(w) * uint64_t(h) * uint64_t(fps + 0.5f);

	// only consider >=5_0 due to general_tier_flag on ptl

	if (pixrate < 267'386'880)
		return STD_VIDEO_H265_LEVEL_IDC_5_0;
	if (pixrate < 534'773'760)
		return STD_VIDEO_H265_LEVEL_IDC_5_1;
	if (pixrate < 1'069'547'520)
		return STD_VIDEO_H265_LEVEL_IDC_6_0;
	if (pixrate < 2'139'095'040)
		return STD_VIDEO_H265_LEVEL_IDC_6_1;
	return STD_VIDEO_H265_LEVEL_IDC_6_2;
}

static uint32_t find_lsb(uint32_t v)
{
	for (int i = 0; i < 32; i++)
		if (v & (1u << i))
			return i;

	throw std::runtime_error(std::string("invalid value in encode capabilities reported by gpu driver"));
}

static uint32_t find_msb(uint32_t v)
{
	for (int i = 31; i >= 0; i--)
		if (v & (1u << i))
			return i;

	throw std::runtime_error(std::string("invalid value in encode capabilities reported by gpu driver"));
}

wivrn::video_encoder_vulkan_h265::video_encoder_vulkan_h265(
        wivrn_vk_bundle & vk,
        vk::Rect2D rect,
        const vk::VideoCapabilitiesKHR & video_caps,
        const vk::VideoEncodeCapabilitiesKHR & encode_caps,
        float fps,
        uint8_t stream_idx,
        const encoder_settings & settings) :
        video_encoder_vulkan(vk, rect, video_caps, encode_caps, fps, stream_idx, settings),
        vui{
                .flags = {
                        .aspect_ratio_info_present_flag = 1,
                        .video_signal_type_present_flag = 1,
                        .video_full_range_flag = 1,
                        .colour_description_present_flag = 1,
                        .chroma_loc_info_present_flag = 1,
                        .vui_timing_info_present_flag = 0,            // no fixed framerate
                        .motion_vectors_over_pic_boundaries_flag = 1, // allow motion vectors that exceed picture boundaries
                        .restricted_ref_pic_lists_flag = 1,           // all slices in a single picture use the same referene
                },
                .aspect_ratio_idc = STD_VIDEO_H265_ASPECT_RATIO_IDC_SQUARE, // _pixels_ are square
                .video_format = 5,                                          // unspecified video format
                .colour_primaries = 1,                                      // BT.709
                .transfer_characteristics = 1,                              // BT.709
                .matrix_coeffs = 1,                                         // BT.709
                .chroma_sample_loc_type_top_field = 0,
                .chroma_sample_loc_type_bottom_field = 0,

        },
        dpb{.max_latency_increase_plus1 = {1}, .max_dec_pic_buffering_minus1 = {uint8_t(num_dpb_slots > 0 ? num_dpb_slots - 1 : 0)}, .max_num_reorder_pics = {0}},
        ptl{.flags = {
                    .general_tier_flag = 1, // 1 if >=STD_VIDEO_H265_LEVEL_IDC_5_0 and we only do 5 and above
                    .general_progressive_source_flag = 1,
                    .general_interlaced_source_flag = 0,
                    .general_non_packed_constraint_flag = 0,
                    .general_frame_only_constraint_flag = 1,
            },
            .general_profile_idc = (settings.bit_depth == 10) ? STD_VIDEO_H265_PROFILE_IDC_MAIN_10 : STD_VIDEO_H265_PROFILE_IDC_MAIN,
            .general_level_idc = choose_level(rect.extent.width, rect.extent.height, fps)},
        vps{
                .flags = {
                        .vps_temporal_id_nesting_flag = 1, // radv breaks without
                        .vps_sub_layer_ordering_info_present_flag = 0,
                        .vps_timing_info_present_flag = 0,
                        .vps_poc_proportional_to_timing_flag = 0,
                },
                .vps_video_parameter_set_id = 0,
                .vps_max_sub_layers_minus1 = 0,
                .reserved1 = 0,
                .reserved2 = 0,
                .vps_num_units_in_tick = 0,
                .vps_time_scale = 0,
                .vps_num_ticks_poc_diff_one_minus1 = 0,
                .reserved3 = 0,
                .pDecPicBufMgr = &dpb,
                .pHrdParameters = nullptr,
                .pProfileTierLevel = &ptl,
        },
        sps{
                .flags = {
                        .sps_temporal_id_nesting_flag = 1,        // radv breaks without
                        .conformance_window_flag = 1,             // conf_win_* are set
                        .amp_enabled_flag = 1,                    // confirmed ok
                        .sample_adaptive_offset_enabled_flag = 0, // off for (ultra) low-latency
                        .strong_intra_smoothing_enabled_flag = 1, // confirmed ok
                        .vui_parameters_present_flag = 1,
                        .sps_range_extension_flag = (settings.bit_depth == 10) ? 1u : 0u,
                },
                .chroma_format_idc = STD_VIDEO_H265_CHROMA_FORMAT_IDC_420,
                .pic_width_in_luma_samples = aligned_extent.width,   // confirmed
                .pic_height_in_luma_samples = aligned_extent.height, // confirmed
                .sps_video_parameter_set_id = 0,
                .sps_max_sub_layers_minus1 = 0,
                .sps_seq_parameter_set_id = 0,
                .bit_depth_luma_minus8 = static_cast<uint8_t>(settings.bit_depth - 8),
                .bit_depth_chroma_minus8 = static_cast<uint8_t>(settings.bit_depth - 8),
                .log2_max_pic_order_cnt_lsb_minus4 = 4,      // arbitrary
                .log2_min_luma_coding_block_size_minus3 = 0, // keep it 0, related values are filled in create()
                .num_short_term_ref_pic_sets = 0,
                .num_long_term_ref_pics_sps = 0,
                .pcm_sample_bit_depth_luma_minus1 = 0,
                .pcm_sample_bit_depth_chroma_minus1 = 0,
                .log2_min_pcm_luma_coding_block_size_minus3 = 0,
                .log2_diff_max_min_pcm_luma_coding_block_size = 0,
                .reserved1 = 0,
                .reserved2 = 0,
                .palette_max_size = 0,
                .delta_palette_max_predictor_size = 0,
                .motion_vector_resolution_control_idc = 0,
                .sps_num_palette_predictor_initializers_minus1 = 0,
                .conf_win_left_offset = 0u,
                .conf_win_right_offset = (aligned_extent.width - rect.extent.width) >> 1, // 4:2:0
                .conf_win_top_offset = 0u,
                .conf_win_bottom_offset = (aligned_extent.height - rect.extent.height) >> 1, // 4:2:0
                .pProfileTierLevel = &ptl,
                .pDecPicBufMgr = &dpb,
                .pScalingLists = nullptr,
                .pShortTermRefPicSet = nullptr,
                .pLongTermRefPicsSps = nullptr,
                .pSequenceParameterSetVui = &vui,
                .pPredictorPaletteEntries = nullptr,
        },
        pps{
                .flags = {
                        .cu_qp_delta_enabled_flag = 1,       // must be 1 or nvidia breaks
                        .transquant_bypass_enabled_flag = 0, // only 1 when tuned for lossless
                        .deblocking_filter_control_present_flag = 1,
                        .pps_range_extension_flag = 0u,
                },

                .pps_pic_parameter_set_id = 0,
                .pps_seq_parameter_set_id = 0,
                .sps_video_parameter_set_id = 0,
                .num_extra_slice_header_bits = 0,
                .num_ref_idx_l0_default_active_minus1 = 0,
                .num_ref_idx_l1_default_active_minus1 = 0,
                .init_qp_minus26 = 0,
                .diff_cu_qp_delta_depth = 0,
                .pps_cb_qp_offset = 0,
                .pps_cr_qp_offset = 0,
                .pps_beta_offset_div2 = 0,
                .pps_tc_offset_div2 = 0,
                .log2_parallel_merge_level_minus2 = 0,
                .diff_cu_chroma_qp_offset_depth = 0,
                .chroma_qp_offset_list_len_minus1 = 0,
                .cb_qp_offset_list = {},
                .cr_qp_offset_list = {},
                .log2_sao_offset_scale_luma = 0,
                .log2_sao_offset_scale_chroma = 0,
                .pps_act_y_qp_offset_plus5 = 0,
                .pps_act_cb_qp_offset_plus5 = 0,
                .pps_act_cr_qp_offset_plus3 = 0,
                .pps_num_palette_predictor_initializers = 0,
                .luma_bit_depth_entry_minus8 = static_cast<uint8_t>(settings.bit_depth - 8),
                .chroma_bit_depth_entry_minus8 = static_cast<uint8_t>(settings.bit_depth - 8),
                .num_tile_columns_minus1 = 0,
                .num_tile_rows_minus1 = 0,
                .reserved1 = 0,
                .reserved2 = 0,
                .column_width_minus1 = {},
                .row_height_minus1 = {},
                .reserved3 = 0,
                .pScalingLists = nullptr,
                .pPredictorPaletteEntries = nullptr,
        }
{
	if (not std::ranges::any_of(vk.device_extensions, [](std::string_view ext) { return ext == VK_KHR_VIDEO_ENCODE_H265_EXTENSION_NAME; }))
	{
		throw std::runtime_error("Vulkan video encode H265 extension not available");
	}
}

std::vector<void *> wivrn::video_encoder_vulkan_h265::setup_slot_info(size_t dpb_size)
{
	dpb_std_info.resize(dpb_size, {});
	dpb_std_slots.reserve(dpb_size);
	std::vector<void *> res;
	for (size_t i = 0; i < dpb_size; ++i)
	{
		dpb_std_slots.push_back({
		        .pStdReferenceInfo = &dpb_std_info[i],
		});
		res.push_back(&dpb_std_slots[i]);
	}

	return res;
}

static auto get_video_caps(vk::raii::PhysicalDevice & phys_dev, int bit_depth)
{
	if (!(bit_depth == 8 || bit_depth == 10))
		throw std::runtime_error("h265 encoder supports 8-bit or 10-bit only");

	vk::StructureChain video_profile_info{
	        vk::VideoProfileInfoKHR{
	                .videoCodecOperation = vk::VideoCodecOperationFlagBitsKHR::eEncodeH265,
	                .chromaSubsampling = vk::VideoChromaSubsamplingFlagBitsKHR::e420,
	                .lumaBitDepth = bit_depth == 10 ? vk::VideoComponentBitDepthFlagBitsKHR::e10 : vk::VideoComponentBitDepthFlagBitsKHR::e8,
	                .chromaBitDepth = bit_depth == 10 ? vk::VideoComponentBitDepthFlagBitsKHR::e10 : vk::VideoComponentBitDepthFlagBitsKHR::e8,
	        },
	        vk::VideoEncodeH265ProfileInfoKHR{
	                .stdProfileIdc = bit_depth == 10 ? STD_VIDEO_H265_PROFILE_IDC_MAIN_10 : STD_VIDEO_H265_PROFILE_IDC_MAIN,
	        },
	        vk::VideoEncodeUsageInfoKHR{
	                .videoUsageHints = vk::VideoEncodeUsageFlagBitsKHR::eStreaming,
	                .videoContentHints = vk::VideoEncodeContentFlagBitsKHR::eRendered,
	                .tuningMode = vk::VideoEncodeTuningModeKHR::eUltraLowLatency,
	        }};

	try
	{
		auto [video_caps, encode_caps, encode_h265_caps] =
		        phys_dev.getVideoCapabilitiesKHR<
		                vk::VideoCapabilitiesKHR,
		                vk::VideoEncodeCapabilitiesKHR,
		                vk::VideoEncodeH265CapabilitiesKHR>(video_profile_info.get());
		return std::make_tuple(video_caps, encode_caps, encode_h265_caps, video_profile_info);
	}
	catch (...)
	{}
	// NVIDIA fails if the structure is there
	video_profile_info.unlink<vk::VideoEncodeUsageInfoKHR>();

	auto [video_caps, encode_caps, encode_h265_caps] =
	        phys_dev.getVideoCapabilitiesKHR<
	                vk::VideoCapabilitiesKHR,
	                vk::VideoEncodeCapabilitiesKHR,
	                vk::VideoEncodeH265CapabilitiesKHR>(video_profile_info.get());
	return std::make_tuple(video_caps, encode_caps, encode_h265_caps, video_profile_info);
}

std::unique_ptr<wivrn::video_encoder_vulkan_h265> wivrn::video_encoder_vulkan_h265::create(
        wivrn_vk_bundle & vk,
        encoder_settings & settings,
        float fps,
        uint8_t stream_idx)
{
	vk::Rect2D rect{
	        .offset = {.x = settings.offset_x, .y = settings.offset_y},
	        .extent = {.width = settings.width, .height = settings.height},
	};

	auto [video_caps, encode_caps, encode_h265_caps, video_profile_info] = get_video_caps(vk.physical_device, settings.bit_depth);

	std::unique_ptr<video_encoder_vulkan_h265> self(
	        new video_encoder_vulkan_h265(vk, rect, video_caps, encode_caps, fps, stream_idx, settings));

	vk::VideoEncodeH265SessionParametersAddInfoKHR add_info{};
	add_info.setStdVPSs(self->vps);
	add_info.setStdSPSs(self->sps);
	add_info.setStdPPSs(self->pps);

	vk::VideoEncodeH265SessionParametersCreateInfoKHR session_params_info{
	        .maxStdVPSCount = 1,
	        .maxStdSPSCount = 1,
	        .maxStdPPSCount = 1,
	        .pParametersAddInfo = &add_info,
	};

	vk::VideoEncodeH265SessionCreateInfoKHR session_create_info{
	        .useMaxLevelIdc = false, // let driver clamp if needed
	};

	if (encode_h265_caps.requiresGopRemainingFrames)
	{
		self->gop_info = vk::VideoEncodeH265GopRemainingFrameInfoKHR{
		        .useGopRemainingFrames = true,
		        .gopRemainingI = 0,
		        .gopRemainingP = std::numeric_limits<uint32_t>::max(),
		        .gopRemainingB = 0,
		};
		self->rc_h265 = vk::VideoEncodeH265RateControlInfoKHR{
		        .pNext = &self->gop_info,
		        .flags = {},
		};
		self->rate_control->pNext = &self->rc_h265;
	}

	self->sps.log2_diff_max_min_luma_coding_block_size = find_msb((uint32_t)encode_h265_caps.ctbSizes) + 1; // First bit is 16x16.
	self->sps.log2_min_luma_transform_block_size_minus2 = find_lsb((uint32_t)encode_h265_caps.transformBlockSizes);
	self->sps.log2_diff_max_min_luma_transform_block_size =
	        find_msb((uint32_t)encode_h265_caps.transformBlockSizes) - find_lsb((uint32_t)encode_h265_caps.transformBlockSizes);

	uint32_t max_transform_hierarchy = (find_msb((uint32_t)encode_h265_caps.ctbSizes) + 4) - (find_lsb((uint32_t)encode_h265_caps.transformBlockSizes) + 2);
	self->sps.max_transform_hierarchy_depth_inter = max_transform_hierarchy;
	self->sps.max_transform_hierarchy_depth_intra = max_transform_hierarchy;

	uint32_t syntax_flags = (uint32_t)encode_h265_caps.stdSyntaxFlags;

	if ((syntax_flags & VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_SET_BIT_KHR) != 0 ||
	    (syntax_flags & VK_VIDEO_ENCODE_H265_STD_TRANSFORM_SKIP_ENABLED_FLAG_UNSET_BIT_KHR) == 0)
	{
		self->pps.flags.transform_skip_enabled_flag = 1;
		self->pps.log2_max_transform_skip_block_size_minus2 = find_msb((uint32_t)encode_h265_caps.transformBlockSizes);
	}

	if ((syntax_flags & VK_VIDEO_ENCODE_H265_STD_CONSTRAINED_INTRA_PRED_FLAG_SET_BIT_KHR) != 0)
		self->pps.flags.constrained_intra_pred_flag = 1;

	if ((syntax_flags & VK_VIDEO_ENCODE_H265_STD_SAMPLE_ADAPTIVE_OFFSET_ENABLED_FLAG_SET_BIT_KHR) != 0)
		self->sample_adaptive_offset_enabled = true;

	self->rate_control_layer.pNext = &self->rc_layer_h265;
	self->init(video_caps, video_profile_info.get(), &session_create_info, &session_params_info);
	return self;
}

std::vector<uint8_t> wivrn::video_encoder_vulkan_h265::get_vps_sps_pps()
{
	vk::VideoEncodeH265SessionParametersGetInfoKHR next{
	        .writeStdVPS = true,
	        .writeStdSPS = true,
	        .writeStdPPS = true,
	        .stdVPSId = 0,
	        .stdSPSId = 0,
	        .stdPPSId = 0,
	};
	return get_encoded_parameters(&next);
}

void wivrn::video_encoder_vulkan_h265::send_idr_data()
{
	auto data = get_vps_sps_pps();
	SendData(data, false, true);
}

void * wivrn::video_encoder_vulkan_h265::encode_info_next(uint32_t frame_num, size_t slot, std::optional<int32_t> ref_slot)
{
	slice_header = {
	        .flags = {
	                .first_slice_segment_in_pic_flag = 1,
	                .slice_sao_luma_flag = sample_adaptive_offset_enabled,
	                .slice_sao_chroma_flag = sample_adaptive_offset_enabled,
	                .num_ref_idx_active_override_flag = ref_slot ? 1u : 0u,
	                .collocated_from_l0_flag = 1,
	        },
	        .slice_type = ref_slot ? STD_VIDEO_H265_SLICE_TYPE_P : STD_VIDEO_H265_SLICE_TYPE_I,
	        .MaxNumMergeCand = 5,
	};
	nalu_slice_info = vk::VideoEncodeH265NaluSliceSegmentInfoKHR{
	        .constantQp = 0,
	        .pStdSliceSegmentHeader = &slice_header,
	};
	reference_lists_info = {
	        .flags = {.ref_pic_list_modification_flag_l0 = 0, .ref_pic_list_modification_flag_l1 = 0},
	        .num_ref_idx_l0_active_minus1 = 0,
	        .num_ref_idx_l1_active_minus1 = 0,
	        .RefPicList0 = {},
	        .RefPicList1 = {},
	        .list_entry_l0 = {},
	        .list_entry_l1 = {},
	};
	std::ranges::fill(reference_lists_info.RefPicList0, STD_VIDEO_H265_NO_REFERENCE_PICTURE);
	std::ranges::fill(reference_lists_info.RefPicList1, STD_VIDEO_H265_NO_REFERENCE_PICTURE);

	const int32_t poc = int32_t(frame_num);

	if (ref_slot)
	{
		const int32_t refPoc = dpb_std_info[*ref_slot].PicOrderCntVal;

		st_rps = {};

		for (uint32_t i = 0; i < poc_history.size(); ++i)
		{
			const uint32_t delta = uint32_t(poc - poc_history[i]) - i; // deltas are accumulative!
			st_rps.delta_poc_s0_minus1[i] = uint16_t(delta - 1);
			if (poc_history[i] == refPoc)
			{
				st_rps.used_by_curr_pic_s0_flag |= (1u << i);
				st_rps.num_negative_pics = uint8_t(i + 1);
				break;
			}
		}
		st_rps.num_positive_pics = 0;
		st_rps.used_by_curr_pic_flag = 0;

		reference_lists_info.num_ref_idx_l0_active_minus1 = 0;
		reference_lists_info.RefPicList0[0] = static_cast<uint8_t>(*ref_slot);

		if (st_rps.used_by_curr_pic_s0_flag == 0)
		{
			// ref_slot is too old → encode an IDR instead
			ref_slot.reset();
			poc_history.clear();
		}
	}
	else
	{
		poc_history.clear();
	}

	std_picture_info = {
	        .flags = {
	                .is_reference = 1,
	                .IrapPicFlag = ref_slot ? 0u : 1u,
	                .used_for_long_term_reference = 0,
	                .discardable_flag = 0,
	                .cross_layer_bla_flag = 0,
	                .pic_output_flag = 1,
	                .no_output_of_prior_pics_flag = ref_slot ? 0u : 1u,
	                .short_term_ref_pic_set_sps_flag = 0u,
	                .slice_temporal_mvp_enabled_flag = 1,
	        },
	        .pic_type = ref_slot ? STD_VIDEO_H265_PICTURE_TYPE_P : STD_VIDEO_H265_PICTURE_TYPE_IDR,
	        .sps_video_parameter_set_id = 0,
	        .pps_seq_parameter_set_id = sps.sps_seq_parameter_set_id,
	        .pps_pic_parameter_set_id = pps.pps_pic_parameter_set_id,
	        .short_term_ref_pic_set_idx = 0,
	        .PicOrderCntVal = int32_t(poc),
	        .TemporalId = 0,
	        .reserved1 = {},
	        .pRefLists = ref_slot ? &reference_lists_info : nullptr,
	        .pShortTermRefPicSet = ref_slot ? &st_rps : nullptr,
	        .pLongTermRefPics = nullptr,
	};

	picture_info = vk::VideoEncodeH265PictureInfoKHR{
	        .naluSliceSegmentEntryCount = 1,
	        .pNaluSliceSegmentEntries = &nalu_slice_info,
	        .pStdPictureInfo = &std_picture_info,
	};

	auto & i = dpb_std_info[slot];
	i.flags = {.used_for_long_term_reference = 0, .unused_for_reference = 0};
	i.pic_type = std_picture_info.pic_type;
	i.PicOrderCntVal = std_picture_info.PicOrderCntVal;
	i.TemporalId = std_picture_info.TemporalId;

	const uint32_t history_limit = std::min<uint32_t>(16u, dpb.max_dec_pic_buffering_minus1[0]);

	if (poc_history.size() < 1 || poc_history.front() != poc)
		poc_history.push_front(poc);
	if (poc_history.size() > history_limit)
		poc_history.pop_back();

	return &picture_info;
}

vk::ExtensionProperties wivrn::video_encoder_vulkan_h265::std_header_version()
{
	vk::ExtensionProperties std_header_version{
	        .specVersion = VK_MAKE_VIDEO_STD_VERSION(1, 0, 0),
	};
	strcpy(std_header_version.extensionName,
	       VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME);
	return std_header_version;
}
