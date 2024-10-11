/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "hand_model.h"
#include "openxr/openxr.h"
#include <ranges>
#include <spdlog/spdlog.h>

hand_model::hand_model(const std::filesystem::path & gltf_path, scene_loader & loader, scene_data & scene)
{
	root_node = scene.new_node();
	root_node->name = gltf_path.stem();

	scene.import(loader(gltf_path), root_node);

	joints.resize(XR_HAND_JOINT_COUNT_EXT);
	// clang-format off
	joints[XR_HAND_JOINT_PALM_EXT]                = node_handle();
	joints[XR_HAND_JOINT_WRIST_EXT]               = scene.find_node(root_node, "wrist");
	joints[XR_HAND_JOINT_THUMB_METACARPAL_EXT]    = scene.find_node(root_node, "thumb-metacarpal");
	joints[XR_HAND_JOINT_THUMB_PROXIMAL_EXT]      = scene.find_node(root_node, "thumb-phalanx-proximal");
	joints[XR_HAND_JOINT_THUMB_DISTAL_EXT]        = scene.find_node(root_node, "thumb-phalanx-distal");
	joints[XR_HAND_JOINT_THUMB_TIP_EXT]           = scene.find_node(root_node, "thumb-tip");
	joints[XR_HAND_JOINT_INDEX_METACARPAL_EXT]    = scene.find_node(root_node, "index-finger-metacarpal");
	joints[XR_HAND_JOINT_INDEX_PROXIMAL_EXT]      = scene.find_node(root_node, "index-finger-phalanx-proximal");
	joints[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT]  = scene.find_node(root_node, "index-finger-phalanx-intermediate");
	joints[XR_HAND_JOINT_INDEX_DISTAL_EXT]        = scene.find_node(root_node, "index-finger-phalanx-distal");
	joints[XR_HAND_JOINT_INDEX_TIP_EXT]           = scene.find_node(root_node, "index-finger-tip");
	joints[XR_HAND_JOINT_MIDDLE_METACARPAL_EXT]   = scene.find_node(root_node, "middle-finger-metacarpal");
	joints[XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT]     = scene.find_node(root_node, "middle-finger-phalanx-proximal");
	joints[XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT] = scene.find_node(root_node, "middle-finger-phalanx-intermediate");
	joints[XR_HAND_JOINT_MIDDLE_DISTAL_EXT]       = scene.find_node(root_node, "middle-finger-phalanx-distal");
	joints[XR_HAND_JOINT_MIDDLE_TIP_EXT]          = scene.find_node(root_node, "middle-finger-tip");
	joints[XR_HAND_JOINT_RING_METACARPAL_EXT]     = scene.find_node(root_node, "ring-finger-metacarpal");
	joints[XR_HAND_JOINT_RING_PROXIMAL_EXT]       = scene.find_node(root_node, "ring-finger-phalanx-proximal");
	joints[XR_HAND_JOINT_RING_INTERMEDIATE_EXT]   = scene.find_node(root_node, "ring-finger-phalanx-intermediate");
	joints[XR_HAND_JOINT_RING_DISTAL_EXT]         = scene.find_node(root_node, "ring-finger-phalanx-distal");
	joints[XR_HAND_JOINT_RING_TIP_EXT]            = scene.find_node(root_node, "ring-finger-tip");
	joints[XR_HAND_JOINT_LITTLE_METACARPAL_EXT]   = scene.find_node(root_node, "pinky-finger-metacarpal");
	joints[XR_HAND_JOINT_LITTLE_PROXIMAL_EXT]     = scene.find_node(root_node, "pinky-finger-phalanx-proximal");
	joints[XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT] = scene.find_node(root_node, "pinky-finger-phalanx-intermediate");
	joints[XR_HAND_JOINT_LITTLE_DISTAL_EXT]       = scene.find_node(root_node, "pinky-finger-phalanx-distal");
	joints[XR_HAND_JOINT_LITTLE_TIP_EXT]          = scene.find_node(root_node, "pinky-finger-tip");
	// clang-format on
}

void hand_model::apply(const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & joints_location)
{
	if (joints_location and xr::hand_tracker::check_flags(*joints_location, XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT, 0))
	{
		root_node->visible = true;

		for (auto && [joint, loc]: std::views::zip(joints, *joints_location))
		{
			if (!joint)
				continue;

			joint->position = {
			        loc.first.pose.position.x,
			        loc.first.pose.position.y,
			        loc.first.pose.position.z};

			joint->orientation = {
			        loc.first.pose.orientation.w,
			        loc.first.pose.orientation.x,
			        loc.first.pose.orientation.y,
			        loc.first.pose.orientation.z};
		}
	}
	else
	{
		root_node->visible = false;
	}
}
