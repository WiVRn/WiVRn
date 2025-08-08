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
#include "video_encoder_vulkan_h264.h"
#include "encoder/encoder_settings.h"
#include "utils/wivrn_vk_bundle.h"

static StdVideoH264LevelIdc compute_level(const StdVideoH264SequenceParameterSet & sps, float fps, uint32_t num_dpb_frames, size_t bitrate)
{
	// H264 required level, table A-1 of specification
	struct limit
	{
		StdVideoH264LevelIdc level;
		size_t macrobloc_per_s;
		size_t frame_size; // in macroblocks
		size_t dpb_size;   // in macroblocks
		size_t bit_rate;   // in kb/s
	};
	const std::array limits = {
	        // clang-format off
	        //    level                        mb_per_s     frame    dpb     bitrate
	        limit{STD_VIDEO_H264_LEVEL_IDC_1_0, 1'485     , 99     , 396    , 64     },
	        limit{STD_VIDEO_H264_LEVEL_IDC_1_1, 3'000     , 396    , 900    , 192    },
	        limit{STD_VIDEO_H264_LEVEL_IDC_1_2, 6'000     , 396    , 2'376  , 384    },
	        limit{STD_VIDEO_H264_LEVEL_IDC_1_3, 11'880    , 396    , 2'376  , 768    },
	        limit{STD_VIDEO_H264_LEVEL_IDC_2_0, 11'880    , 396    , 2'376  , 2'000  },
	        limit{STD_VIDEO_H264_LEVEL_IDC_2_1, 19'800    , 792    , 4'752  , 4'000  },
	        limit{STD_VIDEO_H264_LEVEL_IDC_2_2, 20'250    , 1'620  , 8'100  , 4'000  },
	        limit{STD_VIDEO_H264_LEVEL_IDC_3_0, 40'500    , 1'620  , 8'100  , 10'000 },
	        limit{STD_VIDEO_H264_LEVEL_IDC_3_1, 108'000   , 3'600  , 18'000 , 14'000 },
	        limit{STD_VIDEO_H264_LEVEL_IDC_3_2, 216'000   , 5'120  , 20'480 , 20'000 },
	        limit{STD_VIDEO_H264_LEVEL_IDC_4_0, 245'760   , 8'192  , 32'768 , 20'000 },
	        limit{STD_VIDEO_H264_LEVEL_IDC_4_1, 245'760   , 8'192  , 32'768 , 50'000 },
	        limit{STD_VIDEO_H264_LEVEL_IDC_4_2, 522'240   , 8'704  , 34'816 , 50'000 },
	        limit{STD_VIDEO_H264_LEVEL_IDC_5_0, 589'824   , 22'080 , 110'400, 135'000},
	        limit{STD_VIDEO_H264_LEVEL_IDC_5_1, 983'040   , 36'864 , 184'320, 240'000},
	        limit{STD_VIDEO_H264_LEVEL_IDC_5_2, 2'073'600 , 36'864 , 184'320, 240'000},
	        limit{STD_VIDEO_H264_LEVEL_IDC_6_0, 4'177'920 , 139'264, 696'320, 240'000},
	        limit{STD_VIDEO_H264_LEVEL_IDC_6_1, 8'355'840 , 139'264, 696'320, 480'000},
	        limit{STD_VIDEO_H264_LEVEL_IDC_6_2, 16'711'680, 139'264, 696'320, 800'000},
	        // clang-format on
	};

	const size_t frame_size = (sps.pic_width_in_mbs_minus1 + 1) * (sps.pic_height_in_map_units_minus1 + 1);
	const size_t macroblocks_per_s = frame_size * fps + 1;
	const size_t dpb_size = frame_size * num_dpb_frames;

	for (const auto & level: limits)
	{
		if (level.macrobloc_per_s >= macroblocks_per_s and level.frame_size >= frame_size and level.dpb_size >= dpb_size and 1000 * level.bit_rate >= bitrate)
			return level.level;
	}

	return STD_VIDEO_H264_LEVEL_IDC_6_2;
}

wivrn::video_encoder_vulkan_h264::video_encoder_vulkan_h264(
        wivrn_vk_bundle & vk,
        vk::Rect2D rect,
        const vk::VideoCapabilitiesKHR & video_caps,
        const vk::VideoEncodeCapabilitiesKHR & encode_caps,
        float fps,
        uint8_t stream_idx,
        const encoder_settings & settings) :
        video_encoder_vulkan(vk, rect, video_caps, encode_caps, fps, stream_idx, settings),
        sps{
                .flags =
                        {
                                .constraint_set0_flag = 0,
                                .constraint_set1_flag = 1,
                                .constraint_set2_flag = 0,
                                .constraint_set3_flag = 0,
                                .constraint_set4_flag = 0,
                                .constraint_set5_flag = 0,
                                .direct_8x8_inference_flag = 1,
                                .mb_adaptive_frame_field_flag = 0,
                                .frame_mbs_only_flag = 1,
                                .delta_pic_order_always_zero_flag = 1,
                                .separate_colour_plane_flag = 0,
                                .gaps_in_frame_num_value_allowed_flag = 0,
                                .qpprime_y_zero_transform_bypass_flag = 0,
                                .frame_cropping_flag = (rect.extent.width % 16) || (rect.extent.height) % 16,
                                .seq_scaling_matrix_present_flag = 0,
                                .vui_parameters_present_flag = 0,
                        },
                .profile_idc = STD_VIDEO_H264_PROFILE_IDC_BASELINE,
                .level_idc = STD_VIDEO_H264_LEVEL_IDC_INVALID,
                .chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420,
                .seq_parameter_set_id = 0,
                .bit_depth_luma_minus8 = 0,
                .bit_depth_chroma_minus8 = 0,
                .log2_max_frame_num_minus4 = 0,
                .pic_order_cnt_type = STD_VIDEO_H264_POC_TYPE_2,
                .offset_for_non_ref_pic = 0,
                .offset_for_top_to_bottom_field = 0,
                .log2_max_pic_order_cnt_lsb_minus4 = 0,
                .num_ref_frames_in_pic_order_cnt_cycle = 0,
                .max_num_ref_frames = uint8_t(num_dpb_slots - 1),
                .reserved1 = 0,
                .pic_width_in_mbs_minus1 = (rect.extent.width - 1) / 16,
                .pic_height_in_map_units_minus1 = (rect.extent.height - 1) / 16,
                .frame_crop_left_offset = 0,
                .frame_crop_right_offset = (rect.extent.width % 16) / 2,
                .frame_crop_top_offset = 0,
                .frame_crop_bottom_offset = (rect.extent.height % 16) / 2,
                .reserved2 = 0,
                .pOffsetForRefFrame = nullptr,
                .pScalingLists = nullptr,
                .pSequenceParameterSetVui = nullptr,
        },
        pps{
                .flags =
                        {
                                .transform_8x8_mode_flag = 0,
                                .redundant_pic_cnt_present_flag = 0,
                                .constrained_intra_pred_flag = 0,
                                .deblocking_filter_control_present_flag = 0,
                                .weighted_pred_flag = 0,
                                .bottom_field_pic_order_in_frame_present_flag = 0,
                                .entropy_coding_mode_flag = 0,
                                .pic_scaling_matrix_present_flag = 0,
                        },
                .seq_parameter_set_id = 0,
                .pic_parameter_set_id = 0,
                .num_ref_idx_l0_default_active_minus1 = 0,
                .num_ref_idx_l1_default_active_minus1 = 0,
                .weighted_bipred_idc = STD_VIDEO_H264_WEIGHTED_BIPRED_IDC_DEFAULT,
                .pic_init_qp_minus26 = 0,
                .pic_init_qs_minus26 = 0,
                .chroma_qp_index_offset = 0,
                .second_chroma_qp_index_offset = 0,
                .pScalingLists = nullptr,
        }
{
	sps.level_idc = compute_level(sps, fps, num_dpb_slots, settings.bitrate);
	if (not std::ranges::any_of(vk.device_extensions, [](std::string_view ext) { return ext == VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME; }))
	{
		throw std::runtime_error("Vulkan video encode H264 extension not available");
	}
}

std::vector<void *> wivrn::video_encoder_vulkan_h264::setup_slot_info(size_t dpb_size)
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

auto get_video_caps(vk::raii::PhysicalDevice & phys_dev)
{
	vk::StructureChain video_profile_info{
	        vk::VideoProfileInfoKHR{
	                .videoCodecOperation =
	                        vk::VideoCodecOperationFlagBitsKHR::eEncodeH264,
	                .chromaSubsampling = vk::VideoChromaSubsamplingFlagBitsKHR::e420,
	                .lumaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
	                .chromaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
	        },
	        vk::VideoEncodeH264ProfileInfoKHR{
	                .stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_BASELINE,
	        },
	        vk::VideoEncodeUsageInfoKHR{
	                .videoUsageHints = vk::VideoEncodeUsageFlagBitsKHR::eStreaming,
	                .videoContentHints = vk::VideoEncodeContentFlagBitsKHR::eRendered,
	                .tuningMode = vk::VideoEncodeTuningModeKHR::eUltraLowLatency,
	        }};

	try
	{
		auto [video_caps, encode_caps, encode_h264_caps] =
		        phys_dev.getVideoCapabilitiesKHR<
		                vk::VideoCapabilitiesKHR,
		                vk::VideoEncodeCapabilitiesKHR,
		                vk::VideoEncodeH264CapabilitiesKHR>(video_profile_info.get());

		return std::make_tuple(video_caps, encode_caps, encode_h264_caps, video_profile_info);
	}
	catch (...)
	{}
	// NVIDIA fails if the structure is there
	video_profile_info.unlink<vk::VideoEncodeUsageInfoKHR>();
	auto [video_caps, encode_caps, encode_h264_caps] =
	        phys_dev.getVideoCapabilitiesKHR<
	                vk::VideoCapabilitiesKHR,
	                vk::VideoEncodeCapabilitiesKHR,
	                vk::VideoEncodeH264CapabilitiesKHR>(video_profile_info.get());
	return std::make_tuple(video_caps, encode_caps, encode_h264_caps, video_profile_info);
}

std::unique_ptr<wivrn::video_encoder_vulkan_h264> wivrn::video_encoder_vulkan_h264::create(
        wivrn_vk_bundle & vk,
        encoder_settings & settings,
        float fps,
        uint8_t stream_idx)
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

	if (settings.bit_depth != 8)
		throw std::runtime_error("h264 codec only supports 8-bit encoding");

	auto [video_caps, encode_caps, encode_h264_caps, video_profile_info] = get_video_caps(vk.physical_device);

	std::unique_ptr<video_encoder_vulkan_h264> self(new video_encoder_vulkan_h264(vk, rect, video_caps, encode_caps, fps, stream_idx, settings));

	vk::VideoEncodeH264SessionParametersAddInfoKHR h264_add_info{};
	h264_add_info.setStdSPSs(self->sps);
	h264_add_info.setStdPPSs(self->pps);

	vk::VideoEncodeH264SessionParametersCreateInfoKHR h264_session_params{
	        .maxStdSPSCount = 1,
	        .maxStdPPSCount = 1,
	        .pParametersAddInfo = &h264_add_info,
	};

	vk::VideoEncodeH264SessionCreateInfoKHR session_create_info{
	        .useMaxLevelIdc = false,
	};

	if (encode_h264_caps.requiresGopRemainingFrames)
	{
		self->gop_info = vk::VideoEncodeH264GopRemainingFrameInfoKHR{
		        .useGopRemainingFrames = true,
		        .gopRemainingI = 0,
		        .gopRemainingP = std::numeric_limits<uint32_t>::max(),
		        .gopRemainingB = 0,
		};
		self->rate_control_h264 = vk::VideoEncodeH264RateControlInfoKHR{
		        .pNext = &self->gop_info,
		        .gopFrameCount = std::numeric_limits<uint32_t>::max(),
		        .idrPeriod = std::numeric_limits<uint32_t>::max(),
		};
		self->rate_control->pNext = &self->rate_control_h264;
	}

	self->rate_control_layer.pNext = &self->rate_control_layer_h264;

	self->init(video_caps, video_profile_info.get(), &session_create_info, &h264_session_params);

	return self;
}

std::vector<uint8_t> wivrn::video_encoder_vulkan_h264::get_sps_pps()
{
	vk::VideoEncodeH264SessionParametersGetInfoKHR next{
	        .writeStdSPS = true,
	        .writeStdPPS = true,
	};
	return get_encoded_parameters(&next);
}

void wivrn::video_encoder_vulkan_h264::send_idr_data()
{
	auto data = get_sps_pps();
	SendData(data, false, true);
}

void * wivrn::video_encoder_vulkan_h264::encode_info_next(uint32_t frame_num, size_t slot, std::optional<int32_t> ref_slot)
{
	slice_header = {
	        .flags =
	                {
	                        .direct_spatial_mv_pred_flag = 0, //?
	                        .num_ref_idx_active_override_flag = 0,
	                        .reserved = 0,
	                },
	        .first_mb_in_slice = 0,
	        .slice_type = ref_slot ? STD_VIDEO_H264_SLICE_TYPE_P
	                               : STD_VIDEO_H264_SLICE_TYPE_I,
	        .slice_alpha_c0_offset_div2 = 0,
	        .slice_beta_offset_div2 = 0,
	        .slice_qp_delta = 0,
	        .reserved1 = 0,
	        .cabac_init_idc = STD_VIDEO_H264_CABAC_INIT_IDC_0,
	        .disable_deblocking_filter_idc = STD_VIDEO_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISABLED,
	        .pWeightTable = nullptr,
	};
	nalu_slice_info = vk::VideoEncodeH264NaluSliceInfoKHR{
	        .pStdSliceHeader = &slice_header,
	};
	reference_lists_info = {
	        .flags =
	                {
	                        .ref_pic_list_modification_flag_l0 = 0,
	                        .ref_pic_list_modification_flag_l1 = 0,
	                        .reserved = 0,
	                },
	        .num_ref_idx_l0_active_minus1 = 0,
	        .num_ref_idx_l1_active_minus1 = 0,
	        .RefPicList0 = {},
	        .RefPicList1 = {},
	        .refList0ModOpCount = 0,
	        .refList1ModOpCount = 0,
	        .refPicMarkingOpCount = 0,
	        .reserved1 = {},
	        .pRefList0ModOperations = nullptr,
	        .pRefList1ModOperations = nullptr,
	        .pRefPicMarkingOperations = nullptr,
	};
	std::ranges::fill(reference_lists_info.RefPicList0, STD_VIDEO_H264_NO_REFERENCE_PICTURE);
	std::ranges::fill(reference_lists_info.RefPicList1, STD_VIDEO_H264_NO_REFERENCE_PICTURE);

	if (ref_slot)
		reference_lists_info.RefPicList0[0] = *ref_slot;
	const uint32_t frame_num_mask = ((1 << (sps.log2_max_frame_num_minus4 + 4)) - 1);
	std_picture_info = {
	        .flags =
	                {
	                        .IdrPicFlag = uint32_t(ref_slot ? 0 : 1),
	                        .is_reference = 1,
	                        .no_output_of_prior_pics_flag = 0,
	                        .long_term_reference_flag = 0,
	                        .adaptive_ref_pic_marking_mode_flag = 0,
	                        .reserved = 0,
	                },
	        .seq_parameter_set_id = 0,
	        .pic_parameter_set_id = 0,
	        .idr_pic_id = idr_id,
	        .primary_pic_type = ref_slot ? STD_VIDEO_H264_PICTURE_TYPE_P
	                                     : STD_VIDEO_H264_PICTURE_TYPE_IDR,
	        .frame_num = frame_num & frame_num_mask,
	        .PicOrderCnt = int32_t((2 * frame_num) & ((1 << (sps.log2_max_pic_order_cnt_lsb_minus4 + 4)) - 1)),
	        .temporal_id = 0,
	        .reserved1 = {},
	        .pRefLists = &reference_lists_info,
	};
	picture_info = vk::VideoEncodeH264PictureInfoKHR{
	        .naluSliceEntryCount = 1,
	        .pNaluSliceEntries = &nalu_slice_info,
	        .pStdPictureInfo = &std_picture_info,
	        .generatePrefixNalu = false, // check if useful, check if supported
	};

	auto & i = dpb_std_info[slot];
	i.primary_pic_type = std_picture_info.primary_pic_type;
	i.FrameNum = std_picture_info.frame_num;
	i.PicOrderCnt = std_picture_info.PicOrderCnt;

	if (ref_slot)
	{
		auto ref_frame = dpb_std_info[*ref_slot].FrameNum;
		if (((ref_frame + 1) & frame_num_mask) != i.FrameNum)
		{
			reference_lists_info.flags.ref_pic_list_modification_flag_l0 = 1;
			reference_lists_info.refList0ModOpCount = ref_mod.size();
			reference_lists_info.pRefList0ModOperations = ref_mod.data();
			ref_mod[0] = {
			        .modification_of_pic_nums_idc = STD_VIDEO_H264_MODIFICATION_OF_PIC_NUMS_IDC_SHORT_TERM_SUBTRACT,
			        .abs_diff_pic_num_minus1 = uint16_t(uint16_t(i.FrameNum - ref_frame - 1) & frame_num_mask),
			};
			ref_mod[1] = {
			        .modification_of_pic_nums_idc = STD_VIDEO_H264_MODIFICATION_OF_PIC_NUMS_IDC_END,
			};
		}
	}
	else
		++idr_id;

	return &picture_info;
}
vk::ExtensionProperties wivrn::video_encoder_vulkan_h264::std_header_version()
{
	vk::ExtensionProperties std_header_version{
	        .specVersion = VK_MAKE_VIDEO_STD_VERSION(1, 0, 0),
	};
	strcpy(std_header_version.extensionName,
	       VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME);
	return std_header_version;
}
