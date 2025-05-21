/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Sapphire <imsapphire0@gmail.com>
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

// See https://pico.crx.moe/docs/pico-openxr/eye-gaze-interaction and https://pico.crx.moe/docs/pico-openxr/eye-tracking.

#pragma once

#include <openxr/openxr.h>

#define XR_PICO_EYE_TRACKING_EXTENSION_NAME "XR_PICO_eye_tracking"

typedef XrFlags64 XrTrackingModeFlagsPICO;

static const XrTrackingModeFlagsPICO XR_TRACKING_MODE_EYE_BIT_PICO = 0x00000004;
static const XrTrackingModeFlagsPICO XR_TRACKING_MODE_FACE_BIT_PICO = 0x00000008;
static const XrTrackingModeFlagsPICO XR_TRACKING_MODE_FACE_LIPSYNC_PICO = 0x00002000;
static const XrTrackingModeFlagsPICO XR_TRACKING_MODE_FACE_LIPSYNC_BLEND_SHAPES_PICO = 0x00000100;

typedef enum XrTrackingStateCodePICO
{
	XR_MT_SUCCESS_PICO = 0,
	XR_MT_FAILURE_PICO = -1,
	XR_MT_MODE_NONE_PICO = -2,
	XR_MT_DEVICE_NOT_SUPPORT_PICO = -3,
	XR_MT_SERVICE_NEED_START_PICO = -4,
	XR_MT_ET_PERMISSION_DENIED_PICO = -5,
	XR_MT_FT_PERMISSION_DENIED_PICO = -6,
	XR_MT_MIC_PERMISSION_DENIED_PICO = -7,
	XR_MT_SYSTEM_DENIED_PICO = -8,
	XR_MT_UNKNOWN_ERROR_PICO = -9,
} XrTrackingStateCodePICO;

typedef enum XrFaceTrackingDataTypePICO
{
	XR_GET_FACE_DATA_DEFAULT_PICO = 0,
	XR_GET_FACE_DATA_PICO = 3,
	XR_GET_LIP_DATA_PICO = 4,
	XR_GET_FACELIP_DATA_PICO = 5,
} XrFaceTrackingDataTypePICO;

typedef struct XrFaceTrackingDataPICO
{
	XrTime time;
	float blendShapeWeight[72];
	float isVideoInputValid[10];
	float laughingProbability;
	float emotionProbability[10];
	float reserved[128];
} XrFaceTrackingDataPICO;

typedef enum XrBlendShapeIndexPICO
{
	XR_BS_EYELOOKDOWN_L_PICO = 0,
	XR_BS_NOSESNEER_L_PICO = 1,
	XR_BS_EYELOOKIN_L_PICO = 2,
	XR_BS_BROWINNERUP_PICO = 3,
	XR_BS_BROWDOWN_R_PICO = 4,
	XR_BS_MOUTHCLOSE_PICO = 5,
	XR_BS_MOUTHLOWERDOWN_R_PICO = 6,
	XR_BS_JAWOPEN_PICO = 7,
	XR_BS_MOUTHUPPERUP_R_PICO = 8,
	XR_BS_MOUTHSHRUGUPPER_PICO = 9,
	XR_BS_MOUTHFUNNEL_PICO = 10,
	XR_BS_EYELOOKIN_R_PICO = 11,
	XR_BS_EYELOOKDOWN_R_PICO = 12,
	XR_BS_NOSESNEER_R_PICO = 13,
	XR_BS_MOUTHROLLUPPER_PICO = 14,
	XR_BS_JAWRIGHT_PICO = 15,
	XR_BS_BROWDOWN_L_PICO = 16,
	XR_BS_MOUTHSHRUGLOWER_PICO = 17,
	XR_BS_MOUTHROLLLOWER_PICO = 18,
	XR_BS_MOUTHSMILE_L_PICO = 19,
	XR_BS_MOUTHPRESS_L_PICO = 20,
	XR_BS_MOUTHSMILE_R_PICO = 21,
	XR_BS_MOUTHPRESS_R_PICO = 22,
	XR_BS_MOUTHDIMPLE_R_PICO = 23,
	XR_BS_MOUTHLEFT_PICO = 24,
	XR_BS_JAWFORWARD_PICO = 25,
	XR_BS_EYESQUINT_L_PICO = 26,
	XR_BS_MOUTHFROWN_L_PICO = 27,
	XR_BS_EYEBLINK_L_PICO = 28,
	XR_BS_CHEEKSQUINT_L_PICO = 29,
	XR_BS_BROWOUTERUP_L_PICO = 30,
	XR_BS_EYELOOKUP_L_PICO = 31,
	XR_BS_JAWLEFT_PICO = 32,
	XR_BS_MOUTHSTRETCH_L_PICO = 33,
	XR_BS_MOUTHPUCKER_PICO = 34,
	XR_BS_EYELOOKUP_R_PICO = 35,
	XR_BS_BROWOUTERUP_R_PICO = 36,
	XR_BS_CHEEKSQUINT_R_PICO = 37,
	XR_BS_EYEBLINK_R_PICO = 38,
	XR_BS_MOUTHUPPERUP_L_PICO = 39,
	XR_BS_MOUTHFROWN_R_PICO = 40,
	XR_BS_EYESQUINT_R_PICO = 41,
	XR_BS_MOUTHSTRETCH_R_PICO = 42,
	XR_BS_CHEEKPUFF_PICO = 43,
	XR_BS_EYELOOKOUT_L_PICO = 44,
	XR_BS_EYELOOKOUT_R_PICO = 45,
	XR_BS_EYEWIDE_R_PICO = 46,
	XR_BS_EYEWIDE_L_PICO = 47,
	XR_BS_MOUTHRIGHT_PICO = 48,
	XR_BS_MOUTHDIMPLE_L_PICO = 49,
	XR_BS_MOUTHLOWERDOWN_L_PICO = 50,
	XR_BS_TONGUEOUT_PICO = 51,
	XR_VISEME_PP_PICO = 52,
	XR_VISEME_CH_PICO = 53,
	XR_VISEME_O_PICO = 54,
	XR_VISEME_OU_PICO = 55,
	XR_VISEME_I_BACK_PICO = 56,
	XR_VISEME_U_PICO = 57,
	XR_VISEME_RR_PICO = 58,
	XR_VISEME_XX_PICO = 59,
	XR_VISEME_AA_PICO = 60,
	XR_VISEME_I_FRONT_PICO = 61,
	XR_VISEME_FF_PICO = 62,
	XR_VISEME_UW_PICO = 63,
	XR_VISEME_TH_PICO = 64,
	XR_VISEME_KK_PICO = 65,
	XR_VISEME_SS_PICO = 66,
	XR_VISEME_E_PICO = 67,
	XR_VISEME_DD_PICO = 68,
	XR_VISEME_EI_PICO = 69,
	XR_VISEME_NN_PICO = 70,
	XR_VISEME_SIL_PICO = 71,
	XR_BLEND_SHAPE_COUNT_PICO = 72,
} XrBlendShapeIndexPICO;

typedef XrResult(XRAPI_PTR * PFN_xrStartEyeTrackingPICO)(XrSession session);
typedef XrResult(XRAPI_PTR * PFN_xrStopEyeTrackingPICO)(XrSession session, XrTrackingModeFlagsPICO mode);
typedef XrResult(XRAPI_PTR * PFN_xrSetTrackingModePICO)(XrSession session, XrTrackingModeFlagsPICO flags);
typedef XrResult(XRAPI_PTR * PFN_xrGetFaceTrackingStatePICO)(XrSession session, XrTrackingModeFlagsPICO * mode, XrTrackingStateCodePICO * code);
typedef XrResult(XRAPI_PTR * PFN_xrGetFaceTrackingDataPICO)(XrSession session, XrTime time, XrFaceTrackingDataTypePICO type, XrFaceTrackingDataPICO * data);
