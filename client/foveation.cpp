/*
 * WiVRn VR streaming
 * Copyright (C) 2026 galister <galister-dev@pm.me>
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

#include "foveation.h"

#include "xr/instance.h"
#include "xr/session.h"
#include "xr/system.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
float convergence_angle(float distance, float eye_x, float yaw)
{
	const float target_x = distance * std::sin(yaw);
	const float target_z = distance * std::cos(yaw);

	return std::atan2(target_x - eye_x, target_z);
}

XrVector2f yaw_pitch(const XrQuaternionf & q)
{
	const float sine_theta = std::clamp(-2.0f * (q.y * q.z - q.w * q.x), -1.0f, 1.0f);
	const float pitch = std::asin(sine_theta);

	if (std::abs(sine_theta) > 0.99999f)
	{
		const float scale = std::copysign(2.0f, sine_theta);
		return {scale * std::atan2(-q.z, q.w), pitch};
	}

	return {
	        std::atan2(2.0f * (q.x * q.z + q.w * q.y),
	                   q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z),
	        pitch};
}

float ndc_to_angle(float center, float a0, float a1)
{
	const float u = std::clamp(center * 0.5f + 0.5f, 0.0f, 1.0f);
	const float tangent = std::lerp(std::tan(a0), std::tan(a1), u);
	return std::atan(tangent);
}

} // namespace

namespace client_foveation
{

eye_tracked_center::eye_tracked_center(xr::instance & inst, xr::system & sys, xr::session & xr_session) :
        session(xr_session)
{
	if (inst.has_extension(XR_META_FOVEATION_EYE_TRACKED_EXTENSION_NAME) and
	    inst.has_extension(XR_FB_FOVEATION_EXTENSION_NAME) and
	    inst.has_extension(XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME) and
	    inst.has_extension(XR_FB_FOVEATION_VULKAN_EXTENSION_NAME) and
	    inst.has_extension(XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME) and
	    inst.has_extension(XR_META_VULKAN_SWAPCHAIN_CREATE_INFO_EXTENSION_NAME) and
	    sys.foveation_eye_tracked_properties().supportsFoveationEyeTracked)
	{
		xrCreateFoveationProfileFB = inst.get_proc<PFN_xrCreateFoveationProfileFB>("xrCreateFoveationProfileFB");
		xrDestroyFoveationProfileFB = inst.get_proc<PFN_xrDestroyFoveationProfileFB>("xrDestroyFoveationProfileFB");
		xrUpdateSwapchainFB = inst.get_proc<PFN_xrUpdateSwapchainFB>("xrUpdateSwapchainFB");
		xrGetFoveationEyeTrackedStateMETA = inst.get_proc<PFN_xrGetFoveationEyeTrackedStateMETA>("xrGetFoveationEyeTrackedStateMETA");
	}
}

bool eye_tracked_center::supports_foveation_center() const
{
	return xrCreateFoveationProfileFB and xrDestroyFoveationProfileFB and xrUpdateSwapchainFB and xrGetFoveationEyeTrackedStateMETA;
}

std::optional<std::array<XrVector2f, 2>> eye_tracked_center::get_foveation_center(XrSwapchain swapchain) const
{
	// Meta HMDs only update foveationCenter if the eye-tracked profile is
	// applied to the swapchain and xrUpdateSwapchainFB is called. The fragment
	// density map itself is enumerated by the swapchain setup but is otherwise
	// ignored by WiVRn.
	if (!supports_foveation_center())
		return {};

	if (!foveation_profile)
	{
		XrFoveationEyeTrackedProfileCreateInfoMETA eye_tracked_info{
		        .type = XR_TYPE_FOVEATION_EYE_TRACKED_PROFILE_CREATE_INFO_META,
		        .flags = 0,
		};
		XrFoveationLevelProfileCreateInfoFB level_info{
		        .type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB,
		        .next = &eye_tracked_info,
		        .level = XR_FOVEATION_LEVEL_LOW_FB,
		        .verticalOffset = 0,
		        .dynamic = XR_FOVEATION_DYNAMIC_DISABLED_FB,
		};
		XrFoveationProfileCreateInfoFB profile_info{
		        .type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB,
		        .next = &level_info,
		};

		XrFoveationProfileFB raw_profile = XR_NULL_HANDLE;
		XrResult profile_result = xrCreateFoveationProfileFB(session, &profile_info, &raw_profile);
		if (XR_FAILED(profile_result))
			return {};

		foveation_profile.emplace(raw_profile, xrDestroyFoveationProfileFB);
	}

	XrSwapchainStateFoveationFB swapchain_state{
	        .type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB,
	        .flags = 0,
	        .profile = *foveation_profile,
	};

	XrResult update_result = xrUpdateSwapchainFB(
	        swapchain,
	        reinterpret_cast<const XrSwapchainStateBaseHeaderFB *>(&swapchain_state));
	if (XR_FAILED(update_result))
		return {};

	XrFoveationEyeTrackedStateMETA state{
	        .type = XR_TYPE_FOVEATION_EYE_TRACKED_STATE_META,
	};

	XrResult state_result = xrGetFoveationEyeTrackedStateMETA(session, &state);
	if (XR_FAILED(state_result))
		return {};

	if (!(state.flags & XR_FOVEATION_EYE_TRACKED_STATE_VALID_BIT_META))
		return {};

	return std::array<XrVector2f, 2>{state.foveationCenter[0], state.foveationCenter[1]};
}

angles center_to_angles(const std::array<XrVector2f, 2> & center, std::span<const XrFovf> fovs)
{
	assert(fovs.size() == 2);
	angles result{};

	for (size_t i = 0; i < result.size(); ++i)
	{
		result[i].x = ndc_to_angle(center[i].x, fovs[i].angleLeft, fovs[i].angleRight);
		result[i].y = ndc_to_angle(center[i].y, fovs[i].angleUp, fovs[i].angleDown);
	}

	return result;
}

static angles target_angles(
        float yaw,
        float pitch,
        float convergence_distance,
        std::span<const XrView> views)
{
	assert(views.size() == 2);
	angles result{};

	for (size_t i = 0; i < result.size(); ++i)
	{
		result[i].x = convergence_angle(convergence_distance, views[i].pose.position.x, yaw);
		result[i].y = pitch;
	}

	return result;
}

angles fixed_angles(std::span<const XrView> views, float pitch, float convergence_distance)
{
	return target_angles(0.0f, pitch, convergence_distance, views);
}

static std::optional<angles> eye_gaze_angles(
        const wivrn::from_headset::tracking::pose & gaze_pose,
        XrViewStateFlags view_flags,
        std::span<const XrView> views)
{
	using flags = wivrn::from_headset::pose_flags;
	const uint8_t orientation_ok = flags::orientation_valid | flags::orientation_tracked;
	if ((gaze_pose.flags & orientation_ok) != orientation_ok or
	    not(view_flags & XR_VIEW_STATE_POSITION_VALID_BIT) or
	    views.size() != 2)
		return {};

	const XrVector2f gaze = yaw_pitch(gaze_pose.pose.orientation);
	return target_angles(-gaze.x, gaze.y, default_convergence_distance, views);
}

void update_angles(angle_update_state & state, const angles & value)
{
	if (state.valid and
	    state.value[0].x == value[0].x and state.value[0].y == value[0].y and
	    state.value[1].x == value[1].x and state.value[1].y == value[1].y)
		return;

	state.value = value;
	state.valid = true;
	state.dirty = true;
}

tracking_state::tracking_state(bool use_meta_foveation, bool use_eye_gaze_foveation) :
        use_meta_foveation(use_meta_foveation),
        use_eye_gaze_foveation(use_eye_gaze_foveation)
{}

void tracking_state::begin_packet(bool override_enabled)
{
	if (override_enabled)
	{
		have_meta_angles = false;
		have_eye_gaze_angles = false;
	}
}

std::optional<angles> tracking_state::on_views(
        XrViewStateFlags view_flags,
        std::span<const XrView> views,
        bool override_enabled,
        float override_pitch,
        float override_distance)
{
	const bool views_position_valid = (view_flags & XR_VIEW_STATE_POSITION_VALID_BIT) and views.size() == 2;
	if (not views_position_valid)
		return {};

	if (override_enabled)
		return fixed_angles(views, override_pitch, override_distance);

	if (use_eye_gaze_foveation)
	{
		if (not have_eye_gaze_angles)
			return fixed_angles(views);
		return {};
	}

	if (not use_meta_foveation)
		return fixed_angles(views);

	if (not have_meta_angles)
		return fixed_angles(views);

	return {};
}

std::optional<angles> tracking_state::on_eye_gaze(
        const wivrn::from_headset::tracking::pose & gaze_pose,
        XrViewStateFlags view_flags,
        std::span<const XrView> views,
        bool override_enabled)
{
	if (use_meta_foveation or not use_eye_gaze_foveation or override_enabled)
		return {};

	auto result = eye_gaze_angles(gaze_pose, view_flags, views);
	if (result)
		have_eye_gaze_angles = true;
	return result;
}

std::optional<angles> tracking_state::consume_meta_angles(angle_update_state & state, bool override_enabled)
{
	if (not use_meta_foveation or override_enabled or not state.valid or (not state.dirty and have_meta_angles))
		return {};

	state.dirty = false;
	have_meta_angles = true;
	return state.value;
}

} // namespace client_foveation
