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

#include "xrt_cast.h"
#include "wivrn_packets.h"
#include <cstring>
#include <type_traits>

namespace
{
template <typename Out, typename In>
Out do_cast(const In & in)
{
	Out res;
	static_assert(sizeof(res) == sizeof(in));
	memcpy(&res, &in, sizeof(res));
	return res;
}
} // namespace

#define XRT_CAST(in_type, out_type)           \
	out_type xrt_cast(const in_type & in) \
	{                                     \
		return do_cast<out_type>(in); \
	}

XRT_CAST(XrPosef, xrt_pose)
XRT_CAST(XrVector3f, xrt_vec3)
XRT_CAST(XrQuaternionf, xrt_quat)
XRT_CAST(XrFovf, xrt_fov)

XRT_CAST(xrt_pose, XrPosef)
XRT_CAST(xrt_fov, XrFovf)

xrt_space_relation_flags from_pose_flags(uint8_t in_flags)
{
	std::underlying_type_t<xrt_space_relation_flags> flags{};
	if (in_flags & wivrn::from_headset::pose_flags::orientation_valid)
		flags |= XRT_SPACE_RELATION_ORIENTATION_VALID_BIT;
	if (in_flags & wivrn::from_headset::pose_flags::position_valid)
		flags |= XRT_SPACE_RELATION_POSITION_VALID_BIT;
	if (in_flags & wivrn::from_headset::pose_flags::linear_velocity_valid)
		flags |= XRT_SPACE_RELATION_LINEAR_VELOCITY_VALID_BIT;
	if (in_flags & wivrn::from_headset::pose_flags::angular_velocity_valid)
		flags |= XRT_SPACE_RELATION_ANGULAR_VELOCITY_VALID_BIT;
	if (in_flags & wivrn::from_headset::pose_flags::orientation_tracked)
		flags |= XRT_SPACE_RELATION_ORIENTATION_TRACKED_BIT;
	if (in_flags & wivrn::from_headset::pose_flags::position_tracked)
		flags |= XRT_SPACE_RELATION_POSITION_TRACKED_BIT;
	return xrt_space_relation_flags(flags);
}
