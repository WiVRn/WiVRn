/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <openxr/openxr.h>

struct packed_quaternion
{
	uint32_t value;

	static packed_quaternion from_quaternion(const XrQuaternionf & q)
	{
		assert(std::abs(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z - 1) < 0.01);

		float abs_w = std::abs(q.w);
		float abs_x = std::abs(q.x);
		float abs_y = std::abs(q.y);
		float abs_z = std::abs(q.z);

		const float scale_factor = 511 * std::sqrt(2);

		if (abs_x > std::max({abs_y, abs_z, abs_w}))
		{
			uint32_t q1 = q.y * std::copysign(scale_factor, q.x) + 512.5;
			uint32_t q2 = q.z * std::copysign(scale_factor, q.x) + 512.5;
			uint32_t q3 = q.w * std::copysign(scale_factor, q.x) + 512.5;

			return {(0 << 30) | (q1 << 20) | (q2 << 10) | q3};
		}
		else if (abs_y > std::max({abs_x, abs_z, abs_w}))
		{
			uint32_t q1 = q.x * std::copysign(scale_factor, q.y) + 512.5;
			uint32_t q2 = q.z * std::copysign(scale_factor, q.y) + 512.5;
			uint32_t q3 = q.w * std::copysign(scale_factor, q.y) + 512.5;

			return {(1 << 30) | (q1 << 20) | (q2 << 10) | q3};
		}
		else if (abs_z > std::max({abs_x, abs_y, abs_w}))
		{
			uint32_t q1 = q.x * std::copysign(scale_factor, q.z) + 512.5;
			uint32_t q2 = q.y * std::copysign(scale_factor, q.z) + 512.5;
			uint32_t q3 = q.w * std::copysign(scale_factor, q.z) + 512.5;

			return {(2 << 30) | (q1 << 20) | (q2 << 10) | q3};
		}
		else
		{
			uint32_t q1 = q.x * std::copysign(scale_factor, q.w) + 512.5;
			uint32_t q2 = q.y * std::copysign(scale_factor, q.w) + 512.5;
			uint32_t q3 = q.z * std::copysign(scale_factor, q.w) + 512.5;

			return {(3 << 30) | (q1 << 20) | (q2 << 10) | q3};
		}
	}

	operator XrQuaternionf() const
	{
		const float scale_factor = 1. / (511 * std::sqrt(2));

		float q1 = (int((value >> 20) & 0x3ff) - 512) * scale_factor;
		float q2 = (int((value >> 10) & 0x3ff) - 512) * scale_factor;
		float q3 = (int((value >> 0) & 0x3ff) - 512) * scale_factor;
		float q0 = std::sqrt(1 - q1 * q1 - q2 * q2 - q3 * q3);

		switch (value >> 30)
		{
			case 0:
				return {q0, q1, q2, q3};
			case 1:
				return {q1, q0, q2, q3};
			case 2:
				return {q1, q2, q0, q3};
			case 3:
				return {q1, q2, q3, q0};
		}

		__builtin_unreachable();
	}
};

inline packed_quaternion pack(const XrQuaternionf & q)
{
	return packed_quaternion::from_quaternion(q);
}
