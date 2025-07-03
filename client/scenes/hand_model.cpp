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
#include "render/scene_components.h"
#include <entt/entt.hpp>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>

namespace components
{
struct hand_joint
{
	XrHandEXT hand;
	XrHandJointEXT joint;
};
} // namespace components

static const std::array<std::pair<XrHandJointEXT, std::string_view>, XR_HAND_JOINT_COUNT_EXT - 1 /* No palm in gltf */> joints{{
        // clang-format off
	{XR_HAND_JOINT_WRIST_EXT,               "wrist"},
	{XR_HAND_JOINT_THUMB_METACARPAL_EXT,    "thumb-metacarpal"},
	{XR_HAND_JOINT_THUMB_PROXIMAL_EXT,      "thumb-phalanx-proximal"},
	{XR_HAND_JOINT_THUMB_DISTAL_EXT,        "thumb-phalanx-distal"},
	{XR_HAND_JOINT_THUMB_TIP_EXT,           "thumb-tip"},
	{XR_HAND_JOINT_INDEX_METACARPAL_EXT,    "index-finger-metacarpal"},
	{XR_HAND_JOINT_INDEX_PROXIMAL_EXT,      "index-finger-phalanx-proximal"},
	{XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT,  "index-finger-phalanx-intermediate"},
	{XR_HAND_JOINT_INDEX_DISTAL_EXT,        "index-finger-phalanx-distal"},
	{XR_HAND_JOINT_INDEX_TIP_EXT,           "index-finger-tip"},
	{XR_HAND_JOINT_MIDDLE_METACARPAL_EXT,   "middle-finger-metacarpal"},
	{XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,     "middle-finger-phalanx-proximal"},
	{XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, "middle-finger-phalanx-intermediate"},
	{XR_HAND_JOINT_MIDDLE_DISTAL_EXT,       "middle-finger-phalanx-distal"},
	{XR_HAND_JOINT_MIDDLE_TIP_EXT,          "middle-finger-tip"},
	{XR_HAND_JOINT_RING_METACARPAL_EXT,     "ring-finger-metacarpal"},
	{XR_HAND_JOINT_RING_PROXIMAL_EXT,       "ring-finger-phalanx-proximal"},
	{XR_HAND_JOINT_RING_INTERMEDIATE_EXT,   "ring-finger-phalanx-intermediate"},
	{XR_HAND_JOINT_RING_DISTAL_EXT,         "ring-finger-phalanx-distal"},
	{XR_HAND_JOINT_RING_TIP_EXT,            "ring-finger-tip"},
	{XR_HAND_JOINT_LITTLE_METACARPAL_EXT,   "pinky-finger-metacarpal"},
	{XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,     "pinky-finger-phalanx-proximal"},
	{XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, "pinky-finger-phalanx-intermediate"},
	{XR_HAND_JOINT_LITTLE_DISTAL_EXT,       "pinky-finger-phalanx-distal"},
	{XR_HAND_JOINT_LITTLE_TIP_EXT,          "pinky-finger-tip"},
        // clang-format on
}};

void hand_model::add_hand(scene & scene,
                          XrHandEXT hand,
                          const std::filesystem::path & gltf_path,
                          uint32_t layer_mask)
{
	auto && [entity, node] = scene.load_gltf(gltf_path, layer_mask);

	node.name = gltf_path.stem();

	for (auto [joint, name]: joints)
	{
		entt::entity joint_entity = find_node_by_name(scene.world, name, entity);

		scene.world.emplace<components::hand_joint>(joint_entity, hand, joint);
	}

	// Fake joint to hide the hand when it should not be visible
	scene.world.emplace<components::hand_joint>(entity, hand, (XrHandJointEXT)-1);
}

void hand_model::apply(entt::registry & scene,
                       const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & left_hand,
                       const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & right_hand)
{
	for (auto && [entity, hj, node]: scene.view<components::hand_joint, components::node>().each())
	{
		auto f = [&](const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & joints) {
			if (joints.has_value())
			{
				node.visible = true;

				if (hj.joint < 0 or hj.joint > XR_HAND_JOINT_COUNT_EXT)
					return;

				const auto & pose = (*joints)[hj.joint].first.pose;

				node.position = {
				        pose.position.x,
				        pose.position.y,
				        pose.position.z};

				node.orientation = {
				        pose.orientation.w,
				        pose.orientation.x,
				        pose.orientation.y,
				        pose.orientation.z};
			}
			else
			{
				node.visible = false;
			}
		};

		switch (hj.hand)
		{
			case XR_HAND_LEFT_EXT:
				f(left_hand);
				break;
			case XR_HAND_RIGHT_EXT:
				f(right_hand);
				break;
			default:
				break;
		}
	}
}
