// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "pose_list.h"
#include "wivrn_session.h"

struct tracked_views
{
	XrViewStateFlags flags;
	xrt_space_relation relation = XRT_SPACE_RELATION_ZERO;
	std::array<xrt_pose, 2> poses;
	std::array<xrt_fov, 2> fovs;
};

tracked_views
interpolate(const tracked_views &a, const tracked_views &b, float t);

tracked_views
extrapolate(const tracked_views &a, const tracked_views &b, uint64_t ta, uint64_t tb, uint64_t t);

class view_list : public history<tracked_views>
{
public:
	void
	update_tracking(const from_headset::tracking &tracking, const clock_offset &offset);
};
