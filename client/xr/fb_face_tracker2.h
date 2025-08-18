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

#include "utils/handle.h"
#include "wivrn_packets.h"
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

class fb_face_tracker2 : public utils::handle<XrFaceTracker2FB>
{
	PFN_xrGetFaceExpressionWeights2FB xrGetFaceExpressionWeights2FB{};

public:
	using packet_type = wivrn::from_headset::tracking::fb_face2;
	fb_face_tracker2(instance & inst, session & s);

	void get_weights(XrTime time, packet_type & out_expressions);
};
} // namespace xr
