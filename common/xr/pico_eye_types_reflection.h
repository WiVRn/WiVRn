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
