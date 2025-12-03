/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "history.h"

#include <array>
#include <cmath>
#include <openxr/openxr.h>

namespace wivrn
{

struct clock_offset;
class wivrn_session;

struct wivrn_htc_face_data
{
	int64_t eye_sample_time;
	int64_t lip_sample_time;
	std::array<float, XRT_FACIAL_EXPRESSION_EYE_COUNT_HTC> eye;
	std::array<float, XRT_FACIAL_EXPRESSION_LIP_COUNT_HTC> lip;
	bool eye_active;
	bool lip_active;
};

class htc_face_list : public history<htc_face_list, wivrn_htc_face_data>
{
public:
	static wivrn_htc_face_data interpolate(const wivrn_htc_face_data & a, const wivrn_htc_face_data & b, float t)
	{
		wivrn_htc_face_data result = b;

		if (not a.eye_active)
		{
			result.eye = b.eye;
			result.eye_active = b.eye_active;
		}
		else if (not b.eye_active)
		{
			result.eye = a.eye;
			result.eye_active = a.eye_active;
		}
		else
		{
			result.eye_active = true;
			for (size_t i = 0; i < result.eye.size(); i++)
			{
				result.eye[i] = std::clamp(std::lerp(a.eye[i], b.eye[i], t), 0.0f, 1.0f);
			}
		}

		if (not a.lip_active)
		{
			result.lip = b.lip;
			result.lip_active = b.lip_active;
		}
		else if (not b.lip_active)
		{
			result.lip = a.lip;
			result.lip_active = a.lip_active;
		}
		else
		{
			result.lip_active = true;
			for (size_t i = 0; i < result.lip.size(); i++)
			{
				result.lip[i] = std::clamp(std::lerp(a.lip[i], b.lip[i], t), 0.0f, 1.0f);
			}
		}
		return result;
	}

	bool update_tracking(const XrTime & production_timestamp, const XrTime & timestamp, const wivrn_htc_face_data & data, const clock_offset & offset)
	{
		return this->add_sample(production_timestamp, timestamp, data, offset);
	}
};

class wivrn_htc_face_tracker : public xrt_device
{
	htc_face_list face_list;
	std::array<xrt_input, 2> inputs_array;

	wivrn::wivrn_session & cnx;

public:
	using base = xrt_device;
	wivrn_htc_face_tracker(xrt_device * hmd, wivrn::wivrn_session & cnx);

	xrt_result_t update_inputs();
	void update_tracking(const from_headset::tracking &, const clock_offset &);
	xrt_result_t get_face_tracking(enum xrt_input_name facial_expression_type, int64_t at_timestamp_ns, struct xrt_facial_expression_set * out_value);
};
} // namespace wivrn
