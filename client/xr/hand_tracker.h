/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "utils/handle.h"
#include <array>
#include <optional>
#include <utility>
#include <vector>
#include <openxr/openxr.h>

namespace xr
{
class instance;
class session;

class hand_tracker : public utils::handle<XrHandTrackerEXT>
{
public:
	using joint = std::pair<XrHandJointLocationEXT, XrHandJointVelocityEXT>;
	using joint_array = std::array<joint, XR_HAND_JOINT_COUNT_EXT>;

	struct located_hand
	{
		joint_array joints;
		std::optional<XrHandTrackingDataSourceEXT> data_source;

		// should these joints be used for input?
		// true if UNOBSTRUCTED or XR_EXT_hand_tracking_data_source not supported
		bool is_input_source() const
		{
			return !data_source or *data_source == XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT;
		}
	};

	struct mesh_data
	{
		std::vector<XrPosef> joint_bind_poses;
		std::vector<float> joint_radii;
		std::vector<XrHandJointEXT> joint_parents;
		std::vector<XrVector3f> vertex_positions;
		std::vector<XrVector3f> vertex_normals;
		std::vector<XrVector2f> vertex_uvs;
		std::vector<XrVector4sFB> vertex_blend_indices;
		std::vector<XrVector4f> vertex_blend_weights;
		std::vector<int16_t> indices;
	};

private:
	PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT{};
	PFN_xrGetHandMeshFB xrGetHandMeshFB{};
	bool hand_tracking_data_source_supported = false;
	std::optional<mesh_data> cached_hand_mesh_fb;
	bool hand_mesh_fb_fetched = false;

public:
	hand_tracker(instance & inst, session & session, const XrHandTrackerCreateInfoEXT & info);

	std::optional<located_hand> locate(XrSpace space, XrTime time);
	const mesh_data * mesh();

	static bool check_flags(const joint_array & joints, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity);
};
} // namespace xr
