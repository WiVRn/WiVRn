/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <glm/gtc/quaternion.hpp>
#include <openxr/openxr.h>

inline glm::quat glm_cast(const XrQuaternionf & q)
{
	return glm::quat::wxyz(q.w, q.x, q.y, q.z);
}

inline XrQuaternionf glm_cast(const glm::quat & q)
{
	return XrQuaternionf(q.w, q.x, q.y, q.z);
}

inline glm::vec3 glm_cast(const XrVector3f & v)
{
	return glm::vec3(v.x, v.y, v.z);
}

inline XrVector3f glm_cast(const glm::vec3 & v)
{
	return XrVector3f(v.x, v.y, v.z);
}
