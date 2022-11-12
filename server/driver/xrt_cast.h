// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "xrt/xrt_defines.h"
#include <openxr/openxr.h>

xrt_pose
xrt_cast(const XrPosef &);
xrt_vec3
xrt_cast(const XrVector3f &);
xrt_quat
xrt_cast(const XrQuaternionf &);
xrt_fov
xrt_cast(const XrFovf &);

XrPosef
xrt_cast(const xrt_pose &);
XrFovf
xrt_cast(const xrt_fov &);
