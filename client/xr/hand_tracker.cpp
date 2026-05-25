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

#include "hand_tracker.h"
#include "xr/check.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/to_string.h"
#include <algorithm>
#include <cassert>
#include <new>
#include <spdlog/spdlog.h>

xr::hand_tracker::hand_tracker(instance & inst, session & session, const XrHandTrackerCreateInfoEXT & info) :
        handle(inst.get_proc<PFN_xrDestroyHandTrackerEXT>("xrDestroyHandTrackerEXT"))
{
	auto xrCreateHandTrackerEXT = inst.get_proc<PFN_xrCreateHandTrackerEXT>("xrCreateHandTrackerEXT");
	assert(xrCreateHandTrackerEXT);
	xrLocateHandJointsEXT = inst.get_proc<PFN_xrLocateHandJointsEXT>("xrLocateHandJointsEXT");
	if (inst.has_extension(XR_FB_HAND_TRACKING_MESH_EXTENSION_NAME))
		xrGetHandMeshFB = inst.get_proc<PFN_xrGetHandMeshFB>("xrGetHandMeshFB");
	CHECK_XR(xrCreateHandTrackerEXT(session, &info, &id));
}

std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> xr::hand_tracker::locate(XrSpace space, XrTime time)
{
	if (!id || !xrLocateHandJointsEXT)
		return std::nullopt;

	XrHandJointsLocateInfoEXT info{
	        .type = XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT,
	        .next = nullptr,
	        .baseSpace = space,
	        .time = time,
	};

	std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints_pos;
	std::array<XrHandJointVelocityEXT, XR_HAND_JOINT_COUNT_EXT> joints_vel;

#ifndef NDEBUG
	// Silence the OpenXR validation layer by setting valid flags
	for (auto & i: joints_pos)
		i.locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT;
	for (auto & i: joints_vel)
		i.velocityFlags = XR_SPACE_VELOCITY_LINEAR_VALID_BIT;
#endif

	XrHandJointVelocitiesEXT velocities{
	        .type = XR_TYPE_HAND_JOINT_VELOCITIES_EXT,
	        .next = nullptr,
	        .jointCount = joints_vel.size(),
	        .jointVelocities = joints_vel.data(),
	};

	XrHandJointLocationsEXT locations{
	        .type = XR_TYPE_HAND_JOINT_LOCATIONS_EXT,
	        .next = &velocities,
	        .jointCount = joints_pos.size(),
	        .jointLocations = joints_pos.data(),
	};

	CHECK_XR(xrLocateHandJointsEXT(id, &info, &locations));

	if (!locations.isActive)
		return std::nullopt;

	// bail if any of the joint is invalid
	if (std::ranges::any_of(joints_pos, [](const auto & loc) { return loc.locationFlags == 0; }))
		return std::nullopt;

	std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT> joints;
	for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
	{
		joints[i] = {joints_pos[i], joints_vel[i]};
	}

	return joints;
}

const xr::hand_tracker::mesh_data * xr::hand_tracker::mesh()
{
	if (hand_mesh_fb_fetched)
		return cached_hand_mesh_fb ? &*cached_hand_mesh_fb : nullptr;

	hand_mesh_fb_fetched = true;

	if (!id || !xrGetHandMeshFB)
		return nullptr;

	XrHandTrackingMeshFB mesh{
	        .type = XR_TYPE_HAND_TRACKING_MESH_FB,
	};

	XrResult result = xrGetHandMeshFB(id, &mesh);
	if (XR_FAILED(result))
	{
		spdlog::warn("xrGetHandMeshFB count query failed: {}", xr::to_string(result));
		return nullptr;
	}

	if (mesh.jointCountOutput != XR_HAND_JOINT_COUNT_EXT)
	{
		spdlog::warn("Unsupported hand mesh joint count {}", mesh.jointCountOutput);
		return nullptr;
	}

	if (mesh.jointCountOutput > 32)
	{
		spdlog::warn("Hand mesh joint count {} exceeds renderer limit", mesh.jointCountOutput);
		return nullptr;
	}

	if (mesh.vertexCountOutput == 0 || mesh.indexCountOutput == 0)
	{
		spdlog::warn("Hand mesh data is empty");
		return nullptr;
	}

	mesh_data data;
	try
	{
		data.joint_bind_poses.resize(mesh.jointCountOutput);
		data.joint_radii.resize(mesh.jointCountOutput);
		data.joint_parents.resize(mesh.jointCountOutput);
		data.vertex_positions.resize(mesh.vertexCountOutput);
		data.vertex_normals.resize(mesh.vertexCountOutput);
		data.vertex_uvs.resize(mesh.vertexCountOutput);
		data.vertex_blend_indices.resize(mesh.vertexCountOutput);
		data.vertex_blend_weights.resize(mesh.vertexCountOutput);
		data.indices.resize(mesh.indexCountOutput);
	}
	catch (const std::bad_alloc &)
	{
		spdlog::warn("Hand mesh allocation failed for {} vertices and {} indices", mesh.vertexCountOutput, mesh.indexCountOutput);
		return nullptr;
	}

	mesh.jointCapacityInput = data.joint_bind_poses.size();
	mesh.jointBindPoses = data.joint_bind_poses.data();
	mesh.jointRadii = data.joint_radii.data();
	mesh.jointParents = data.joint_parents.data();
	mesh.vertexCapacityInput = data.vertex_positions.size();
	mesh.vertexPositions = data.vertex_positions.data();
	mesh.vertexNormals = data.vertex_normals.data();
	mesh.vertexUVs = data.vertex_uvs.data();
	mesh.vertexBlendIndices = data.vertex_blend_indices.data();
	mesh.vertexBlendWeights = data.vertex_blend_weights.data();
	mesh.indexCapacityInput = data.indices.size();
	mesh.indices = data.indices.data();

	result = xrGetHandMeshFB(id, &mesh);
	if (XR_FAILED(result))
	{
		spdlog::warn("xrGetHandMeshFB data query failed: {}", xr::to_string(result));
		return nullptr;
	}

	auto count_changed = [](const char * what, uint32_t output, size_t expected) {
		if (output == expected)
			return false;
		spdlog::warn("Hand mesh {} count changed from {} to {}", what, expected, output);
		return true;
	};

	if (count_changed("joint", mesh.jointCountOutput, data.joint_bind_poses.size()) ||
	    count_changed("vertex", mesh.vertexCountOutput, data.vertex_positions.size()) ||
	    count_changed("index", mesh.indexCountOutput, data.indices.size()))
	{
		return nullptr;
	}

	if (mesh.indexCountOutput % 3 != 0)
	{
		spdlog::warn("Hand mesh index count {} is not a triangle list", mesh.indexCountOutput);
		return nullptr;
	}

	const int joint_count = static_cast<int>(mesh.jointCountOutput);
	if (!std::ranges::all_of(data.vertex_blend_indices, [joint_count](const XrVector4sFB & blend_indices) {
			return 0 <= blend_indices.x && blend_indices.x < joint_count &&
			       0 <= blend_indices.y && blend_indices.y < joint_count &&
			       0 <= blend_indices.z && blend_indices.z < joint_count &&
			       0 <= blend_indices.w && blend_indices.w < joint_count;
		}))
	{
		spdlog::warn("Hand mesh has out-of-range blend indices");
		return nullptr;
	}

	const int vertex_count = static_cast<int>(mesh.vertexCountOutput);
	if (!std::ranges::all_of(data.indices, [vertex_count](int16_t index) {
			return 0 <= index && index < vertex_count;
		}))
	{
		spdlog::warn("Hand mesh has out-of-range indices");
		return nullptr;
	}

	cached_hand_mesh_fb = std::move(data);
	return &*cached_hand_mesh_fb;
}

bool xr::hand_tracker::check_flags(const std::array<joint, XR_HAND_JOINT_COUNT_EXT> & joints, XrSpaceLocationFlags position, XrSpaceVelocityFlags velocity)
{
	return std::ranges::all_of(joints, [position, velocity](const auto & joint) {
		return (joint.first.locationFlags & position) == position and (joint.second.velocityFlags & velocity) == velocity;
	});
}
