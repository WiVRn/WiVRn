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

#pragma once

#include "utils/handle.h"
#include "wivrn_packets.h"

#include <array>
#include <optional>
#include <span>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;
class system;
} // namespace xr

namespace client_foveation
{
using angles = std::array<XrVector2f, 2>;

inline constexpr float default_convergence_distance = 1.0f;
// Default foveation target is 1 m forward and 10 degrees below horizontal.
inline constexpr float default_foveation_pitch = -0.17453292519943295f; // -10 degrees

struct angle_update_state
{
	angles value{};
	bool valid = false;
	bool dirty = false;
};

struct manual_override
{
	bool enabled = false;
	float pitch = default_foveation_pitch;
	float distance = default_convergence_distance;
};

class eye_tracked_center
{
	XrSession session = XR_NULL_HANDLE;
	PFN_xrCreateFoveationProfileFB xrCreateFoveationProfileFB = nullptr;
	PFN_xrDestroyFoveationProfileFB xrDestroyFoveationProfileFB = nullptr;
	PFN_xrUpdateSwapchainFB xrUpdateSwapchainFB = nullptr;
	PFN_xrGetFoveationEyeTrackedStateMETA xrGetFoveationEyeTrackedStateMETA = nullptr;
	mutable std::optional<utils::handle<XrFoveationProfileFB>> foveation_profile;

public:
	eye_tracked_center() = default;
	eye_tracked_center(xr::instance &, xr::system &, xr::session &);

	bool supports_foveation_center() const;
	std::optional<std::array<XrVector2f, 2>> get_foveation_center(XrSwapchain swapchain) const;
};

angles center_to_angles(const std::array<XrVector2f, 2> & center, std::span<const XrFovf> fovs);
angles fixed_angles(std::span<const XrView> views, float pitch = default_foveation_pitch, float convergence_distance = default_convergence_distance);
void update_angles(angle_update_state & state, const angles & value);

class tracking_state
{
	bool use_meta_foveation;
	bool use_eye_gaze_foveation;
	bool have_meta_angles = false;
	bool have_eye_gaze_angles = false;

public:
	tracking_state(bool use_meta_foveation, bool use_eye_gaze_foveation);

	void begin_packet(bool override_enabled);
	std::optional<angles> on_views(
	        XrViewStateFlags view_flags,
	        std::span<const XrView> views,
	        bool override_enabled,
	        float override_pitch,
	        float override_distance);
	std::optional<angles> on_eye_gaze(
	        const wivrn::from_headset::tracking::pose & gaze_pose,
	        XrViewStateFlags view_flags,
	        std::span<const XrView> views,
	        bool override_enabled);
	std::optional<angles> consume_meta_angles(angle_update_state & state, bool override_enabled);
};

} // namespace client_foveation
