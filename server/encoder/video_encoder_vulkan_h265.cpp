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
#include "util/u_logging.h"
#include "utils/wivrn_vk_bundle.h"

wivrn::video_encoder_vulkan_h265::video_encoder_vulkan_h265(wivrn_vk_bundle & vk, vk::Rect2D rect, vk::VideoEncodeCapabilitiesKHR encode_caps, float fps, uint64_t bitrate) :
        video_encoder_vulkan(vk, rect, encode_caps, fps, bitrate),
        vps{
                .flags{
                        .vps_temporal_id_nesting_flag = 0,
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
        },
        sps{
                .flags{
                        .sps_temporal_id_nesting_flag = 0,
                        .separate_colour_plane_flag = 0,
                        .conformance_window_flag = 0,
                        .sps_sub_layer_ordering_info_present_flag = 0,
                        .scaling_list_enabled_flag = 0,
                        .sps_scaling_list_data_present_flag = 0,
                        .amp_enabled_flag = 0,
                        .sample_adaptive_offset_enabled_flag = 0,
                        .pcm_enabled_flag = 0,
                        .pcm_loop_filter_disabled_flag = 0,
                        .long_term_ref_pics_present_flag = 0,
                        .sps_temporal_mvp_enabled_flag = 0,
                        .strong_intra_smoothing_enabled_flag = 0,
                        .vui_parameters_present_flag = 0,
                        .sps_extension_present_flag = 0,
                        .sps_range_extension_flag = 0,
                        .transform_skip_rotation_enabled_flag = 0,
                        .transform_skip_context_enabled_flag = 0,
                        .implicit_rdpcm_enabled_flag = 0,
                        .explicit_rdpcm_enabled_flag = 0,
                        .extended_precision_processing_flag = 0,
                        .intra_smoothing_disabled_flag = 0,
                        .high_precision_offsets_enabled_flag = 0,
                        .persistent_rice_adaptation_enabled_flag = 0,
                        .cabac_bypass_alignment_enabled_flag = 0,
                        .sps_scc_extension_flag = 0,
                        .sps_curr_pic_ref_enabled_flag = 0,
                        .palette_mode_enabled_flag = 0,
                        .sps_palette_predictor_initializers_present_flag = 0,
                        .intra_boundary_filtering_disabled_flag = 0,
                },
                .chroma_format_idc = STD_VIDEO_H265_CHROMA_FORMAT_IDC_420,
                .pic_width_in_luma_samples = 0,
                .pic_height_in_luma_samples = 0,
                .sps_video_parameter_set_id = 0,
                .sps_max_sub_layers_minus1 = 0,
                .sps_seq_parameter_set_id = 0,
                .bit_depth_luma_minus8 = 0,
                .bit_depth_chroma_minus8 = 0,
                .log2_max_pic_order_cnt_lsb_minus4 = 0,
                .log2_min_luma_coding_block_size_minus3 = 0,
                .log2_diff_max_min_luma_coding_block_size = 0,
                .log2_min_luma_transform_block_size_minus2 = 0,
                .log2_diff_max_min_luma_transform_block_size = 0,
                .max_transform_hierarchy_depth_inter = 0,
                .max_transform_hierarchy_depth_intra = 0,
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
                .conf_win_left_offset = 0,
                .conf_win_right_offset = 0,
                .conf_win_top_offset = 0,
                .conf_win_bottom_offset = 0,
        },
        pps{
                .flags =
                        {
                                .dependent_slice_segments_enabled_flag = 0,
                                .output_flag_present_flag = 0,
                                .sign_data_hiding_enabled_flag = 0,
                                .cabac_init_present_flag = 0,
                                .constrained_intra_pred_flag = 0,
                                .transform_skip_enabled_flag = 0,
                                .cu_qp_delta_enabled_flag = 0,
                                .pps_slice_chroma_qp_offsets_present_flag = 0,
                                .weighted_pred_flag = 0,
                                .weighted_bipred_flag = 0,
                                .transquant_bypass_enabled_flag = 0,
                                .tiles_enabled_flag = 0,
                                .entropy_coding_sync_enabled_flag = 0,
                                .uniform_spacing_flag = 0,
                                .loop_filter_across_tiles_enabled_flag = 0,
                                .pps_loop_filter_across_slices_enabled_flag = 0,
                                .deblocking_filter_control_present_flag = 0,
                                .deblocking_filter_override_enabled_flag = 0,
                                .pps_deblocking_filter_disabled_flag = 0,
                                .pps_scaling_list_data_present_flag = 0,
                                .lists_modification_present_flag = 0,
                                .slice_segment_header_extension_present_flag = 0,
                                .pps_extension_present_flag = 0,
                                .cross_component_prediction_enabled_flag = 0,
                                .chroma_qp_offset_list_enabled_flag = 0,
                                .pps_curr_pic_ref_enabled_flag = 0,
                                .residual_adaptive_colour_transform_enabled_flag = 0,
                                .pps_slice_act_qp_offsets_present_flag = 0,
                                .pps_palette_predictor_initializers_present_flag = 0,
                                .monochrome_palette_flag = 0,
                                .pps_range_extension_flag = 0,
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
                .log2_max_transform_skip_block_size_minus2 = 0,
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
                .luma_bit_depth_entry_minus8 = 0,
                .chroma_bit_depth_entry_minus8 = 0,
                .num_tile_columns_minus1 = 0,
                .num_tile_rows_minus1 = 0,
                .reserved1 = 0,
                .reserved2 = 0,
                .column_width_minus1 = {},
                .row_height_minus1 = {},
                .reserved3 = 0,
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

std::unique_ptr<wivrn::video_encoder_vulkan_h265> wivrn::video_encoder_vulkan_h265::create(
        wivrn_vk_bundle & vk,
        encoder_settings & settings,
        float fps)
{
	vk::Rect2D rect{
	        .offset = {
	                .x = settings.offset_x,
	                .y = settings.offset_y,
	        },
	        .extent = {
	                .width = settings.width,
	                .height = settings.height,
	        },
	};

	auto [video_caps, encode_caps, encode_h265_caps] =
	        vk.physical_device.getVideoCapabilitiesKHR<
	                vk::VideoCapabilitiesKHR,
	                vk::VideoEncodeCapabilitiesKHR,
	                vk::VideoEncodeH265CapabilitiesKHR>(video_profile_info.get());

	std::unique_ptr<video_encoder_vulkan_h265> self(new video_encoder_vulkan_h265(vk, rect, encode_caps, fps, settings.bitrate));

	vk::VideoEncodeH265SessionParametersAddInfoKHR h265_add_info{};
	h265_add_info.setStdVPSs(self->vps);
	h265_add_info.setStdSPSs(self->sps);
	h265_add_info.setStdPPSs(self->pps);

	vk::VideoEncodeH265SessionParametersCreateInfoKHR h265_session_params{
	        .maxStdVPSCount = 1,
	        .maxStdSPSCount = 1,
	        .maxStdPPSCount = 1,
	        .pParametersAddInfo = &h265_add_info,
	};

	vk::VideoEncodeH265SessionCreateInfoKHR session_create_info{
	        .useMaxLevelIdc = false,
	};

	if (encode_h265_caps.requiresGopRemainingFrames)
	{
		self->gop_info = vk::VideoEncodeH265GopRemainingFrameInfoKHR{
		        .useGopRemainingFrames = true,
		        .gopRemainingI = 0,
		        .gopRemainingP = std::numeric_limits<uint32_t>::max(),
		        .gopRemainingB = 0,
		};
		self->rate_control_h265 = vk::VideoEncodeH265RateControlInfoKHR{
		        .pNext = &self->gop_info,
		        .gopFrameCount = std::numeric_limits<uint32_t>::max(),
		        .idrPeriod = std::numeric_limits<uint32_t>::max(),
		};
		self->rate_control->pNext = &self->rate_control_h265;
	}

	self->init(video_caps, video_profile_info.get(), &session_create_info, &h265_session_params);

	return self;
}

std::vector<uint8_t> wivrn::video_encoder_vulkan_h265::get_vps_sps_pps()
{
	vk::VideoEncodeH265SessionParametersGetInfoKHR next{
	        .writeStdVPS = true,
	        .writeStdSPS = true,
	        .writeStdPPS = true,
	};
	return get_encoded_parameters(&next);
}

void wivrn::video_encoder_vulkan_h265::send_idr_data()
{
	auto data = get_vps_sps_pps();
	SendData(data, false);
}

void * wivrn::video_encoder_vulkan_h265::encode_info_next(uint32_t frame_num, size_t slot, std::optional<size_t> ref)
{
	slice_header = {
	        .flags =
	                {
	                        .first_slice_segment_in_pic_flag = 0,
	                        .dependent_slice_segment_flag = 0,
	                        .slice_sao_luma_flag = 0,
	                        .slice_sao_chroma_flag = 0,
	                        .num_ref_idx_active_override_flag = 0,
	                        .mvd_l1_zero_flag = 0,
	                        .cabac_init_flag = 0,
	                        .cu_chroma_qp_offset_enabled_flag = 0,
	                        .deblocking_filter_override_flag = 0,
	                        .slice_deblocking_filter_disabled_flag = 0,
	                        .collocated_from_l0_flag = 0,
	                        .slice_loop_filter_across_slices_enabled_flag = 0,
	                },
	        .slice_type = ref ? STD_VIDEO_H265_SLICE_TYPE_P
	                          : STD_VIDEO_H265_SLICE_TYPE_I,
	        .slice_segment_address = 0,
	        .collocated_ref_idx = 0,
	        .MaxNumMergeCand = 0,
	        .slice_cb_qp_offset = 0,
	        .slice_cr_qp_offset = 0,
	        .slice_beta_offset_div2 = 0,
	        .slice_tc_offset_div2 = 0,
	        .slice_act_y_qp_offset = 0,
	        .slice_act_cb_qp_offset = 0,
	        .slice_act_cr_qp_offset = 0,
	        .slice_qp_delta = 0,
	};
	nalu_slice_info = vk::VideoEncodeH265NaluSliceSegmentInfoKHR{
	        .pStdSliceSegmentHeader = &slice_header,
	};
	reference_lists_info = {
	        .flags =
	                {
	                        .ref_pic_list_modification_flag_l0 = 0,
	                        .ref_pic_list_modification_flag_l1 = 0,
	                },
	        .num_ref_idx_l0_active_minus1 = 0,
	        .num_ref_idx_l1_active_minus1 = 0,
	        .RefPicList0 = {},
	        .RefPicList1 = {},
	        .list_entry_l0 = {},
	        .list_entry_l1 = {},
	};
	std::fill(reference_lists_info.RefPicList0,
	          reference_lists_info.RefPicList0 + sizeof(reference_lists_info.RefPicList0),
	          STD_VIDEO_H265_NO_REFERENCE_PICTURE);
	std::fill(reference_lists_info.RefPicList1,
	          reference_lists_info.RefPicList1 + sizeof(reference_lists_info.RefPicList1),
	          STD_VIDEO_H265_NO_REFERENCE_PICTURE);
	if (ref)
	{
		reference_lists_info.RefPicList0[0] = *ref;
	}

	std_picture_info = {
	        .flags =
	                {
	                        .is_reference = 1,
	                        .IrapPicFlag = uint32_t(ref ? 0 : 1),
	                        .used_for_long_term_reference = 0,
	                        .discardable_flag = 0,
	                        .cross_layer_bla_flag = 0,
	                        .pic_output_flag = 0,
	                        .no_output_of_prior_pics_flag = 0,
	                        .short_term_ref_pic_set_sps_flag = 0,
	                        .slice_temporal_mvp_enabled_flag = 0,
	                },
	        .pic_type = ref ? STD_VIDEO_H265_PICTURE_TYPE_P
	                        : STD_VIDEO_H265_PICTURE_TYPE_IDR,
	        .sps_video_parameter_set_id = 0,
	        .pps_seq_parameter_set_id = 0,
	        .pps_pic_parameter_set_id = 0,
	        .short_term_ref_pic_set_idx = 0,
	        .PicOrderCntVal = 0,
	        .TemporalId = 0,
	};
	picture_info = vk::VideoEncodeH265PictureInfoKHR{
	        .naluSliceSegmentEntryCount = 1,
	        .pNaluSliceSegmentEntries = &nalu_slice_info,
	        .pStdPictureInfo = &std_picture_info,
	};

	dpb_std_info[slot].pic_type = std_picture_info.pic_type;

	if (not ref)
		++idr_id;

	return &picture_info;
}
vk::ExtensionProperties wivrn::video_encoder_vulkan_h265::std_header_version()
{
	// FIXME: update to version 1.0
	vk::ExtensionProperties std_header_version{
	        .specVersion = 0x0000900b, // VK_MAKE_VIDEO_STD_VERSION(1, 0, 0),
	};
	strcpy(std_header_version.extensionName,
	       VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME);
	return std_header_version;
}

decltype(wivrn::video_encoder_vulkan_h265::video_profile_info) wivrn::video_encoder_vulkan_h265::video_profile_info = vk::StructureChain{
        vk::VideoProfileInfoKHR{
                .videoCodecOperation =
                        vk::VideoCodecOperationFlagBitsKHR::eEncodeH265,
                .chromaSubsampling = vk::VideoChromaSubsamplingFlagBitsKHR::e420,
                .lumaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
                .chromaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
        },
        vk::VideoEncodeH265ProfileInfoKHR{
                .stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN,
        },
        vk::VideoEncodeUsageInfoKHR{
                .videoUsageHints = vk::VideoEncodeUsageFlagBitsKHR::eStreaming,
                .videoContentHints = vk::VideoEncodeContentFlagBitsKHR::eRendered,
                .tuningMode = vk::VideoEncodeTuningModeKHR::eUltraLowLatency,
        }};
vk::StructureChain video_profile_info{
        vk::VideoProfileInfoKHR{
                .videoCodecOperation =
                        vk::VideoCodecOperationFlagBitsKHR::eEncodeH265,
                .chromaSubsampling = vk::VideoChromaSubsamplingFlagBitsKHR::e420,
                .lumaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
                .chromaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
        },
        vk::VideoEncodeH265ProfileInfoKHR{
                .stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN,
        },
        vk::VideoEncodeUsageInfoKHR{
                .videoUsageHints = vk::VideoEncodeUsageFlagBitsKHR::eStreaming,
                .videoContentHints = vk::VideoEncodeContentFlagBitsKHR::eRendered,
                .tuningMode = vk::VideoEncodeTuningModeKHR::eUltraLowLatency,
        }};
