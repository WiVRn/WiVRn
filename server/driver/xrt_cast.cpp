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
#include <cstring>

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
