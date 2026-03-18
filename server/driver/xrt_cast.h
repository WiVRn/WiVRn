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

#pragma once

#include "xrt/xrt_defines.h"
#include <openxr/openxr.h>

xrt_pose xrt_cast(const XrPosef &);
xrt_vec3 xrt_cast(const XrVector3f &);
xrt_quat xrt_cast(const XrQuaternionf &);
xrt_fov xrt_cast(const XrFovf &);

XrPosef xrt_cast(const xrt_pose &);
XrFovf xrt_cast(const xrt_fov &);

xrt_space_relation_flags from_pose_flags(uint8_t in_flags);
