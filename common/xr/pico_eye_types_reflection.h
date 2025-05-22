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

#define XR_LIST_ENUM_XrTrackingStateCodePICO(_) \
	_(XR_MT_SUCCESS_PICO, 0)                \
	_(XR_MT_FAILURE_PICO, -1)               \
	_(XR_MT_MODE_NONE_PICO, -2)             \
	_(XR_MT_DEVICE_NOT_SUPPORT_PICO, -3)    \
	_(XR_MT_SERVICE_NEED_START_PICO, -4)    \
	_(XR_MT_ET_PERMISSION_DENIED_PICO, -5)  \
	_(XR_MT_FT_PERMISSION_DENIED_PICO, -6)  \
	_(XR_MT_MIC_PERMISSION_DENIED_PICO, -7) \
	_(XR_MT_SYSTEM_DENIED_PICO, -8)         \
	_(XR_MT_UNKNOWN_ERROR_PICO, -9)

#define XR_LIST_ENUM_XrBlendShapeIndexPICO(_) \
	_(XR_BS_EYELOOKDOWN_L_PICO, 0)        \
	_(XR_BS_NOSESNEER_L_PICO, 1)          \
	_(XR_BS_EYELOOKIN_L_PICO, 2)          \
	_(XR_BS_BROWINNERUP_PICO, 3)          \
	_(XR_BS_BROWDOWN_R_PICO, 4)           \
	_(XR_BS_MOUTHCLOSE_PICO, 5)           \
	_(XR_BS_MOUTHLOWERDOWN_R_PICO, 6)     \
	_(XR_BS_JAWOPEN_PICO, 7)              \
	_(XR_BS_MOUTHUPPERUP_R_PICO, 8)       \
	_(XR_BS_MOUTHSHRUGUPPER_PICO, 9)      \
	_(XR_BS_MOUTHFUNNEL_PICO, 10)         \
	_(XR_BS_EYELOOKIN_R_PICO, 11)         \
	_(XR_BS_EYELOOKDOWN_R_PICO, 12)       \
	_(XR_BS_NOSESNEER_R_PICO, 13)         \
	_(XR_BS_MOUTHROLLUPPER_PICO, 14)      \
	_(XR_BS_JAWRIGHT_PICO, 15)            \
	_(XR_BS_BROWDOWN_L_PICO, 16)          \
	_(XR_BS_MOUTHSHRUGLOWER_PICO, 17)     \
	_(XR_BS_MOUTHROLLLOWER_PICO, 18)      \
	_(XR_BS_MOUTHSMILE_L_PICO, 19)        \
	_(XR_BS_MOUTHPRESS_L_PICO, 20)        \
	_(XR_BS_MOUTHSMILE_R_PICO, 21)        \
	_(XR_BS_MOUTHPRESS_R_PICO, 22)        \
	_(XR_BS_MOUTHDIMPLE_R_PICO, 23)       \
	_(XR_BS_MOUTHLEFT_PICO, 24)           \
	_(XR_BS_JAWFORWARD_PICO, 25)          \
	_(XR_BS_EYESQUINT_L_PICO, 26)         \
	_(XR_BS_MOUTHFROWN_L_PICO, 27)        \
	_(XR_BS_EYEBLINK_L_PICO, 28)          \
	_(XR_BS_CHEEKSQUINT_L_PICO, 29)       \
	_(XR_BS_BROWOUTERUP_L_PICO, 30)       \
	_(XR_BS_EYELOOKUP_L_PICO, 31)         \
	_(XR_BS_JAWLEFT_PICO, 32)             \
	_(XR_BS_MOUTHSTRETCH_L_PICO, 33)      \
	_(XR_BS_MOUTHPUCKER_PICO, 34)         \
	_(XR_BS_EYELOOKUP_R_PICO, 35)         \
	_(XR_BS_BROWOUTERUP_R_PICO, 36)       \
	_(XR_BS_CHEEKSQUINT_R_PICO, 37)       \
	_(XR_BS_EYEBLINK_R_PICO, 38)          \
	_(XR_BS_MOUTHUPPERUP_L_PICO, 39)      \
	_(XR_BS_MOUTHFROWN_R_PICO, 40)        \
	_(XR_BS_EYESQUINT_R_PICO, 41)         \
	_(XR_BS_MOUTHSTRETCH_R_PICO, 42)      \
	_(XR_BS_CHEEKPUFF_PICO, 43)           \
	_(XR_BS_EYELOOKOUT_L_PICO, 44)        \
	_(XR_BS_EYELOOKOUT_R_PICO, 45)        \
	_(XR_BS_EYEWIDE_R_PICO, 46)           \
	_(XR_BS_EYEWIDE_L_PICO, 47)           \
	_(XR_BS_MOUTHRIGHT_PICO, 48)          \
	_(XR_BS_MOUTHDIMPLE_L_PICO, 49)       \
	_(XR_BS_MOUTHLOWERDOWN_L_PICO, 50)    \
	_(XR_BS_TONGUEOUT_PICO, 51)           \
	_(XR_VISEME_PP_PICO, 52)              \
	_(XR_VISEME_CH_PICO, 53)              \
	_(XR_VISEME_O_PICO, 54)               \
	_(XR_VISEME_OU_PICO, 55)              \
	_(XR_VISEME_I_BACK_PICO, 56)          \
	_(XR_VISEME_U_PICO, 57)               \
	_(XR_VISEME_RR_PICO, 58)              \
	_(XR_VISEME_XX_PICO, 59)              \
	_(XR_VISEME_AA_PICO, 60)              \
	_(XR_VISEME_I_FRONT_PICO, 61)         \
	_(XR_VISEME_FF_PICO, 62)              \
	_(XR_VISEME_UW_PICO, 63)              \
	_(XR_VISEME_TH_PICO, 64)              \
	_(XR_VISEME_KK_PICO, 65)              \
	_(XR_VISEME_SS_PICO, 66)              \
	_(XR_VISEME_E_PICO, 67)               \
	_(XR_VISEME_DD_PICO, 68)              \
	_(XR_VISEME_EI_PICO, 69)              \
	_(XR_VISEME_NN_PICO, 70)              \
	_(XR_VISEME_SIL_PICO, 71)             \
	_(XR_BLEND_SHAPE_COUNT_PICO, 72)
