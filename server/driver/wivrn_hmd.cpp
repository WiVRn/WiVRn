/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "wivrn_hmd.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "util/u_device.h"
#include "util/u_distortion_mesh.h"
#include "util/u_logging.h"

#include "xrt_cast.h"
#include <algorithm>
#include <cstdint>
#include <stdio.h>
#include <openxr/openxr.h>

#include "configuration.h"

/*
 *
 * Structs and defines.
 *
 */

/*!
 * A wivrn HMD device.
 *
 * @implements xrt_device
 */

/*
 *
 * Functions
 *
 */

static void wivrn_hmd_destroy(xrt_device * xdev);

static void wivrn_hmd_update_inputs(xrt_device * xdev);

static void wivrn_hmd_get_tracked_pose(xrt_device * xdev,
                                       xrt_input_name name,
                                       uint64_t at_timestamp_ns,
                                       xrt_space_relation * out_relation);

static void wivrn_hmd_get_view_poses(xrt_device * xdev,
                                     const xrt_vec3 * default_eye_relation,
                                     uint64_t at_timestamp_ns,
                                     uint32_t view_count,
                                     xrt_space_relation * out_head_relation,
                                     xrt_fov * out_fovs,
                                     xrt_pose * out_poses);

static double foveate(double a, double b, double λ, double c, double x)
{
	// In order to save encoding, transmit and decoding time, only a portion of the image is encoded in full resolution.
	// on each axis, foveated coordinates are defined by the following formula.
	return λ / a * tan(a * x + b) + c;
	// a and b are defined such as:
	// edges of the image are not moved
	// f(-1) = -1
	// f( 1) =  1
	// the function also enforces pixel ratio 1:1 at fovea
	// df⁻¹(x)/dx = 1/scale for x = c
}

static std::tuple<float, float> solve_foveation(float λ, float c)
{
	// Compute a and b for the foveation function such that:
	//   foveate(a, b, scale, c, -1) = -1   (eq. 1)
	//   foveate(a, b, scale, c,  1) =  1   (eq. 2)
	//
	// Use eq. 2 to express a as function of b, then replace in eq. 1
	// equation that needs to be null is:
	auto b = [λ, c](double a) { return atan(a * (1 - c) / λ) - a;};
	auto eq = [λ, c](double a) { return atan(a * (1 - c) / λ) + atan(a * (1 + c) / λ) - 2 * a; }; // (eq. 3)

	// function starts positive, reaches a maximum then decreases to -∞
	double a0 = 0;
	// Find a negative value by computing eq(2^n)
	double a1 = 1;
	while (eq(a1) > 0)
		a1 *= 2;

	// last computed values for f(a0) and f(a1)
	std::optional<double> f_a0;
	double f_a1 = eq(a1);

	int n = 0;
	double a;
	while (std::abs(a1 - a0) > 0.0000001 && n++ < 100)
	{
		if (not f_a0)
		{
			// use binary search
			a = 0.5 * (a0 + a1);
			double val = eq(a);
			if (val > 0)
			{
				a0 = a;
				f_a0 = val;
			}
			else
			{
				a1 = a;
				f_a1 = val;
			}
		}
		else
		{
			// f(a1) is always defined
			// when f(a0) is defined, use secant method
			a = a1 - f_a1 * (a1 - a0) / (f_a1 - *f_a0);
			a0 = a1;
			a1 = a;
			f_a0 = f_a1;
			f_a1 = eq(a);
		}
	}

	return {a, b(a)};
}

bool wivrn_hmd::wivrn_hmd_compute_distortion(xrt_device * xdev, uint32_t view_index, float u, float v, xrt_uv_triplet * result)
{
	// u,v are in the output coordinates (sent to the encoder)
	// result is in the input coordinates (from the application)
	const auto & param = ((wivrn_hmd *)xdev)->foveation_parameters[view_index];
	xrt_vec2 out;

	if (param.x.scale < 1)
	{
		u = 2 * u - 1;

		out.x = foveate(param.x.a, param.x.b, param.x.scale, param.x.center, u);
		out.x = std::clamp<float>((1 + out.x) / 2, 0, 1);
	}
	else
	{
		out.x = u;
	}

	if (param.y.scale < 1)
	{
		v = 2 * v - 1;

		out.y = foveate(param.y.a, param.y.b, param.y.scale, param.y.center, v);
		out.y = std::clamp<float>((1 + out.y) / 2, 0, 1);
	}
	else
	{
		out.y = v;
	}

	result->r = out;
	result->g = out;
	result->b = out;

	return true;
}

wivrn_hmd::wivrn_hmd(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx,
                     const from_headset::headset_info_packet & info) :
        xrt_device{}, cnx(cnx)
{
	xrt_device * base = this;

	base->hmd = &hmd_parts;
	base->tracking_origin = &tracking_origin;
	tracking_origin.type = XRT_TRACKING_TYPE_OTHER;
	tracking_origin.offset.orientation = xrt_quat{0, 0, 0, 1};
	strcpy(tracking_origin.name, "No tracking");

	base->update_inputs = wivrn_hmd_update_inputs;
	base->get_tracked_pose = wivrn_hmd_get_tracked_pose;
	base->get_view_poses = wivrn_hmd_get_view_poses;
	base->destroy = wivrn_hmd_destroy;
	name = XRT_DEVICE_GENERIC_HMD;
	device_type = XRT_DEVICE_TYPE_HMD;
	orientation_tracking_supported = true;
	// hand_tracking_supported = true;
	position_tracking_supported = true;

	// Print name.
	strcpy(str, "WiVRn HMD");
	strcpy(serial, "WiVRn HMD");

	// Setup input.
	pose_input.name = XRT_INPUT_GENERIC_HEAD_POSE;
	pose_input.active = true;
	inputs = &pose_input;
	input_count = 1;

	const auto config = configuration::read_user_configuration();

	auto eye_width = info.recommended_eye_width;   // * config.scale.value_or(1);
	auto eye_height = info.recommended_eye_height; // * config.scale.value_or(1);
	auto scale = config.scale.value_or(std::array<double, 2>{1., 1.});
	int foveated_eye_width = info.recommended_eye_width * scale[0];
	int foveated_eye_height = info.recommended_eye_height * scale[1];
	foveated_eye_height += foveated_eye_height % 2;

	// Setup info.
	hmd->blend_modes[0] = XRT_BLEND_MODE_OPAQUE;
	hmd->blend_mode_count = 1;
	hmd->distortion.models = XRT_DISTORTION_MODEL_COMPUTE;
	hmd->distortion.preferred = XRT_DISTORTION_MODEL_COMPUTE;
	hmd->screens[0].w_pixels = foveated_eye_width * 2;
	hmd->screens[0].h_pixels = foveated_eye_height;
	hmd->screens[0].nominal_frame_interval_ns = 1000000000 / info.preferred_refresh_rate;

	// Left
	hmd->views[0].display.w_pixels = eye_width;
	hmd->views[0].display.h_pixels = eye_height;
	hmd->views[0].viewport.x_pixels = 0;
	hmd->views[0].viewport.y_pixels = 0;
	hmd->views[0].viewport.w_pixels = foveated_eye_width;
	hmd->views[0].viewport.h_pixels = foveated_eye_height;
	hmd->views[0].rot = u_device_rotation_ident;

	// Right
	hmd->views[1].display.w_pixels = eye_width;
	hmd->views[1].display.h_pixels = eye_height;
	hmd->views[1].viewport.x_pixels = foveated_eye_width;
	hmd->views[1].viewport.y_pixels = 0;
	hmd->views[1].viewport.w_pixels = foveated_eye_width;
	hmd->views[1].viewport.h_pixels = foveated_eye_height;
	hmd->views[1].rot = u_device_rotation_ident;

	// FOV from headset info packet
	hmd->distortion.fov[0] = xrt_cast(info.fov[0]);
	hmd->distortion.fov[1] = xrt_cast(info.fov[1]);

	for (int i = 0; i < 2; ++i)
	{
		foveation_parameters[i].x.scale = scale[0];
		foveation_parameters[i].y.scale = scale[1];
		const auto & view = hmd->views[i];
		const auto & fov = hmd->distortion.fov[i];
		float l = tan(fov.angle_left);
		float r = tan(fov.angle_right);
		float t = tan(fov.angle_up);
		float b = tan(fov.angle_down);
		if (scale[0] < 1)
		{
			float cu = (r + l) / (l - r);
			foveation_parameters[i].x.center = cu;

			std::tie(foveation_parameters[i].x.a, foveation_parameters[i].x.b) = solve_foveation(scale[0], cu);
		}

		if (scale[1] < 1)
		{
			float cv = (t + b) / (t - b);
			foveation_parameters[i].y.center = cv;

			std::tie(foveation_parameters[i].y.a, foveation_parameters[i].y.b) = solve_foveation(scale[1], cv);
		}
	}

	// Distortion information
	compute_distortion = wivrn_hmd_compute_distortion;
	u_distortion_mesh_fill_in_compute(this);
}

void wivrn_hmd::update_inputs()
{
	// Empty
}

xrt_space_relation wivrn_hmd::get_tracked_pose(xrt_input_name name, uint64_t at_timestamp_ns)
{
	if (name != XRT_INPUT_GENERIC_HEAD_POSE)
	{
		U_LOG_E("Unknown input name");
		return {};
	}

	return views.get_at(at_timestamp_ns).relation;
}

void wivrn_hmd::update_tracking(const from_headset::tracking & tracking, const clock_offset & offset)
{
	views.update_tracking(tracking, offset);
}

void wivrn_hmd::get_view_poses(const xrt_vec3 * default_eye_relation,
                               uint64_t at_timestamp_ns,
                               uint32_t view_count,
                               xrt_space_relation * out_head_relation,
                               xrt_fov * out_fovs,
                               xrt_pose * out_poses)
{
	auto view = views.get_at(at_timestamp_ns);

	int flags = view.relation.relation_flags;

	if (not(view.flags & XR_VIEW_STATE_POSITION_VALID_BIT))
		flags &= ~XRT_SPACE_RELATION_POSITION_VALID_BIT;

	if (not(view.flags & XR_VIEW_STATE_ORIENTATION_VALID_BIT))
		flags &= ~XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;

	if (not(view.flags & XR_VIEW_STATE_POSITION_TRACKED_BIT))
		flags &= ~XRT_SPACE_RELATION_POSITION_TRACKED_BIT;

	if (not(view.flags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT))
		flags &= ~XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;

	view.relation.relation_flags = (xrt_space_relation_flags)flags;
	*out_head_relation = view.relation;

	assert(view_count == 2);
	for (size_t eye = 0; eye < 2; ++eye)
	{
		out_fovs[eye] = view.fovs[eye];
		out_poses[eye] = view.poses[eye];
	}
}

/*
 *
 * Functions
 *
 */

static void wivrn_hmd_destroy(xrt_device * xdev)
{
	static_cast<wivrn_hmd *>(xdev)->unregister();
}

static void wivrn_hmd_update_inputs(xrt_device * xdev)
{
	static_cast<wivrn_hmd *>(xdev)->update_inputs();
}

static void wivrn_hmd_get_tracked_pose(xrt_device * xdev,
                                       xrt_input_name name,
                                       uint64_t at_timestamp_ns,
                                       xrt_space_relation * out_relation)
{
	*out_relation = static_cast<wivrn_hmd *>(xdev)->get_tracked_pose(name, at_timestamp_ns);
}

static void wivrn_hmd_get_view_poses(xrt_device * xdev,
                                     const xrt_vec3 * default_eye_relation,
                                     uint64_t at_timestamp_ns,
                                     uint32_t view_count,
                                     xrt_space_relation * out_head_relation,
                                     xrt_fov * out_fovs,
                                     xrt_pose * out_poses)
{
	static_cast<wivrn_hmd *>(xdev)->get_view_poses(default_eye_relation, at_timestamp_ns, view_count, out_head_relation, out_fovs, out_poses);
}
