/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2024  galister <galister@librevr.org>
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

#pragma once

#include "wivrn_packets.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"

#include "history.h"

#include <array>
#include <cmath>

namespace wivrn
{

struct clock_offset;
class wivrn_session;

struct wivrn_android_face_data
{
	std::array<float, XRT_FACE_PARAMETER_COUNT_ANDROID> parameters;
	std::array<float, XRT_FACE_REGION_CONFIDENCE_COUNT_ANDROID> confidences;
	xrt_face_tracking_state_android state;
	int64_t sample_time;
	bool is_calibrated;
	bool is_valid;
};

class android_face_list : public history<android_face_list, wivrn_android_face_data>
{
public:
	static wivrn_android_face_data interpolate(const wivrn_android_face_data & a, const wivrn_android_face_data & b, float t)
	{
		if (not a.is_valid)
		{
			// in case neither is valid, both will be zeroed,
			// so return the later one for timestamp's sake
			return b;
		}
		else if (not b.is_valid)
		{
			return a;
		}

		wivrn_android_face_data result = b;

		for (size_t i = 0; i < result.parameters.size(); i++)
		{
			result.parameters[i] = std::clamp(std::lerp(a.parameters[i], b.parameters[i], t), 0.0f, 1.0f);
		}
		for (size_t i = 0; i < result.confidences.size(); i++)
		{
			result.confidences[i] = std::clamp(std::lerp(a.confidences[i], b.confidences[i], t), 0.0f, 1.0f);
		}
		return result;
	}

	void update_tracking(const XrTime & production_timestamp, const XrTime & timestamp, const wivrn_android_face_data & data, const clock_offset & offset)
	{
		add_sample(production_timestamp, timestamp, data, offset);
	}
};

class wivrn_android_face_tracker : public xrt_device
{
	android_face_list face_list;
	xrt_input face_input;

	wivrn::wivrn_session & cnx;

public:
	using base = xrt_device;
	wivrn_android_face_tracker(xrt_device * hmd, wivrn::wivrn_session & cnx);

	xrt_result_t update_inputs();
	void update_tracking(const from_headset::tracking &, const clock_offset &);
	xrt_result_t get_face_tracking(enum xrt_input_name facial_expression_type, int64_t at_timestamp_ns, struct xrt_facial_expression_set * out_value);
	xrt_result_t get_face_calibration_state_android(bool * out_face_is_calibrated);
};
} // namespace wivrn
