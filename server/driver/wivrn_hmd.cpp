// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

// Copyright 2020, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  WiVRn HMD device.
 * @author Jakob Bornecrantz <jakob@collabora.com>
 * @ingroup drv_wivrn
 */

#include "wivrn_hmd.h"

#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "os/os_time.h"

#include "math/m_api.h"
#include "math/m_mathinclude.h"

#include "util/u_var.h"
#include "util/u_misc.h"
#include "util/u_time.h"
#include "util/u_debug.h"
#include "util/u_device.h"
#include "util/u_logging.h"
#include "util/u_distortion_mesh.h"

#include <openxr/openxr.h>
#include <stdio.h>
#include <stdatomic.h>

#include "wivrn_comp_target.h"

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


static void
wivrn_hmd_destroy(xrt_device *xdev);

static void
wivrn_hmd_update_inputs(xrt_device *xdev);

static void
wivrn_hmd_get_tracked_pose(xrt_device *xdev,
                           xrt_input_name name,
                           uint64_t at_timestamp_ns,
                           xrt_space_relation *out_relation);

static void
wivrn_hmd_get_view_poses(xrt_device *xdev,
                         const xrt_vec3 *default_eye_relation,
                         uint64_t at_timestamp_ns,
                         uint32_t view_count,
                         xrt_space_relation *out_head_relation,
                         xrt_fov *out_fovs,
                         xrt_pose *out_poses);

static void
wivrn_hmd_create_compositor_target(struct xrt_device *xdev,
                                   struct comp_compositor *comp,
                                   struct comp_target **out_target);


wivrn_hmd::wivrn_hmd(std::shared_ptr<xrt::drivers::wivrn::wivrn_session> cnx,
                     const from_headset::headset_info_packet &info)
    : xrt_device{}, cnx(cnx)
{
	xrt_device *base = this;

	base->hmd = &hmd_parts;
	base->tracking_origin = &tracking_origin;
	tracking_origin.type = XRT_TRACKING_TYPE_NONE;
	tracking_origin.offset.orientation = xrt_quat{0, 0, 0, 1};
	strcpy(tracking_origin.name, "No tracking");

	base->update_inputs = wivrn_hmd_update_inputs;
	base->get_tracked_pose = wivrn_hmd_get_tracked_pose;
	base->get_view_poses = wivrn_hmd_get_view_poses;
	base->create_compositor_target = wivrn_hmd_create_compositor_target;
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

	auto eye_width = info.recommended_eye_width;
	auto eye_height = info.recommended_eye_height;
	fps = info.preferred_refresh_rate;

	// Setup info.
	hmd->blend_modes[0] = XRT_BLEND_MODE_OPAQUE;
	hmd->blend_mode_count = 1;
	hmd->distortion.models = XRT_DISTORTION_MODEL_NONE;
	hmd->distortion.preferred = XRT_DISTORTION_MODEL_NONE;
	hmd->screens[0].w_pixels = info.recommended_eye_width * 2;
	hmd->screens[0].h_pixels = eye_height;
	hmd->screens[0].nominal_frame_interval_ns = 1000000000 / fps;

	// Left
	hmd->views[0].display.w_pixels = eye_width;
	hmd->views[0].display.h_pixels = eye_height;
	hmd->views[0].viewport.x_pixels = 0;
	hmd->views[0].viewport.y_pixels = 0;
	hmd->views[0].viewport.w_pixels = eye_width;
	hmd->views[0].viewport.h_pixels = eye_height;
	hmd->views[0].rot = u_device_rotation_ident;

	// Right
	hmd->views[1].display.w_pixels = eye_width;
	hmd->views[1].display.h_pixels = eye_height;
	hmd->views[1].viewport.x_pixels = eye_width;
	hmd->views[1].viewport.y_pixels = 0;
	hmd->views[1].viewport.w_pixels = eye_width;
	hmd->views[1].viewport.h_pixels = eye_height;
	hmd->views[1].rot = u_device_rotation_ident;

	// Default FOV from Oculus Quest
	hmd->distortion.fov[0].angle_left = -52 * M_PI / 180;
	hmd->distortion.fov[0].angle_right = 42 * M_PI / 180;
	hmd->distortion.fov[0].angle_up = 47 * M_PI / 180;
	hmd->distortion.fov[0].angle_down = -53 * M_PI / 180;

	hmd->distortion.fov[1].angle_left = -42 * M_PI / 180;
	hmd->distortion.fov[1].angle_right = 52 * M_PI / 180;
	hmd->distortion.fov[1].angle_up = 47 * M_PI / 180;
	hmd->distortion.fov[1].angle_down = -53 * M_PI / 180;

	// Distortion information, fills in xdev->compute_distortion().
	u_distortion_mesh_set_none(this);
}

void
wivrn_hmd::update_inputs()
{
	// Empty
}


xrt_space_relation
wivrn_hmd::get_tracked_pose(xrt_input_name name, uint64_t at_timestamp_ns)
{
	if (name != XRT_INPUT_GENERIC_HEAD_POSE) {
		U_LOG_E("Unknown input name");
		return {};
	}

	return views.get_at(at_timestamp_ns).relation;
}

void
wivrn_hmd::update_tracking(const from_headset::tracking &tracking, const clock_offset &offset)
{
	views.update_tracking(tracking, offset);
}

void
wivrn_hmd::get_view_poses(const xrt_vec3 *default_eye_relation,
                          uint64_t at_timestamp_ns,
                          uint32_t view_count,
                          xrt_space_relation *out_head_relation,
                          xrt_fov *out_fovs,
                          xrt_pose *out_poses)
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
	for (size_t eye = 0; eye < 2; ++eye) {
		out_fovs[eye] = view.fovs[eye];
		out_poses[eye] = view.poses[eye];
	}
}

comp_target *
wivrn_hmd::create_compositor_target(struct comp_compositor *comp)
{
	comp_target *target = comp_target_wivrn_create(cnx, fps);

	target->c = comp;
	return target;
}



/*
 *
 * Functions
 *
 */

static void
wivrn_hmd_destroy(xrt_device *xdev)
{
	static_cast<wivrn_hmd *>(xdev)->unregister();
}

static void
wivrn_hmd_update_inputs(xrt_device *xdev)
{
	static_cast<wivrn_hmd *>(xdev)->update_inputs();
}

static void
wivrn_hmd_get_tracked_pose(xrt_device *xdev,
                           xrt_input_name name,
                           uint64_t at_timestamp_ns,
                           xrt_space_relation *out_relation)
{
	*out_relation = static_cast<wivrn_hmd *>(xdev)->get_tracked_pose(name, at_timestamp_ns);
}

static void
wivrn_hmd_get_view_poses(xrt_device *xdev,
                         const xrt_vec3 *default_eye_relation,
                         uint64_t at_timestamp_ns,
                         uint32_t view_count,
                         xrt_space_relation *out_head_relation,
                         xrt_fov *out_fovs,
                         xrt_pose *out_poses)
{
	static_cast<wivrn_hmd *>(xdev)->get_view_poses(default_eye_relation, at_timestamp_ns, view_count,
	                                               out_head_relation, out_fovs, out_poses);
}

static void
wivrn_hmd_create_compositor_target(struct xrt_device *xdev,
                                   struct comp_compositor *comp,
                                   struct comp_target **out_target)
{
	*out_target = static_cast<wivrn_hmd *>(xdev)->create_compositor_target(comp);
}
