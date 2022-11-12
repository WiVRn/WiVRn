// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#include "xrt_cast.h"
#include <cstring>

namespace {
template <typename Out, typename In>
Out
do_cast(const In &in)
{
	Out res;
	static_assert(sizeof(res) == sizeof(in));
	memcpy(&res, &in, sizeof(res));
	return res;
}
} // namespace

#define XRT_CAST(in_type, out_type)                                                                                    \
	out_type xrt_cast(const in_type &in)                                                                           \
	{                                                                                                              \
		return do_cast<out_type>(in);                                                                          \
	}

XRT_CAST(XrPosef, xrt_pose)
XRT_CAST(XrVector3f, xrt_vec3)
XRT_CAST(XrQuaternionf, xrt_quat)
XRT_CAST(XrFovf, xrt_fov)

XRT_CAST(xrt_pose, XrPosef)
XRT_CAST(xrt_fov, XrFovf)
