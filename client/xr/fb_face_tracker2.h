/*
 * WiVRn VR streaming
 * Copyright (C) 2024  galister <galister@librevr.org>
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

#include "../common/wivrn_packets.h"
#include "utils/handle.h"
#include <array>
#include <optional>
#include <utility>
#include <openxr/openxr.h>

#include "instance.h"

namespace xr
{
XrResult destroy_fb_face_tracker2(XrFaceTracker2FB);

class fb_face_tracker2 : public utils::handle<XrFaceTracker2FB, destroy_fb_face_tracker2>
{
	PFN_xrGetFaceExpressionWeights2FB xrGetFaceExpressionWeights2FB{};
	PFN_xrDestroyFaceTracker2FB xrDestroyFaceTracker2FB{};

public:
	fb_face_tracker2() = default;
	fb_face_tracker2(instance & inst, XrFaceTracker2FB h);
	fb_face_tracker2(fb_face_tracker2 &&) = default;
	fb_face_tracker2 & operator=(fb_face_tracker2 &&) = default;

	~fb_face_tracker2()
	{
		if (id != XR_NULL_HANDLE && xrDestroyFaceTracker2FB)
			xrDestroyFaceTracker2FB(id);
	}

	void get_weights(XrTime time, struct xrt::drivers::wivrn::from_headset::fb_face2 * out_expressions);
};
} // namespace xr
