/*
 * WiVRn VR streaming
 * Copyright (C) 2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2023  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <cstdint>

#include <Eigen/Core>

namespace xrt::drivers::wivrn
{

struct clock_offset;

namespace from_headset
{
struct timesync_response;
}

class offset_estimator
{
	Eigen::Vector3d filtered_U = Eigen::Vector3d::Zero();
	Eigen::Matrix3d A = Eigen::Matrix3d::Zero();

public:
	clock_offset get_offset(const from_headset::timesync_response & packet, int64_t now, clock_offset old_offset);
};

} // namespace xrt::drivers::wivrn

