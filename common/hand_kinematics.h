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

#include <array>
#include <cstdint>
#include <optional>

class hand_kinematics
{
public:
	// Redefine XrHandJoint to avoid depending on OpenXR headers
	enum XrHandJoint
	{
		HAND_JOINT_PALM = 0,  // Midpoint of HAND_JOINT_WRIST and HAND_JOINT_MIDDLE_PROXIMAL
		HAND_JOINT_WRIST = 1, // Root of the hand skeleton

		// The following joints are compressed in a hand packet
		HAND_JOINT_THUMB_METACARPAL = 2,
		HAND_JOINT_THUMB_PROXIMAL = 3,
		HAND_JOINT_THUMB_DISTAL = 4,
		HAND_JOINT_THUMB_TIP = 5,
		HAND_JOINT_INDEX_METACARPAL = 6,
		HAND_JOINT_INDEX_PROXIMAL = 7,
		HAND_JOINT_INDEX_INTERMEDIATE = 8,
		HAND_JOINT_INDEX_DISTAL = 9,
		HAND_JOINT_INDEX_TIP = 10,
		HAND_JOINT_MIDDLE_METACARPAL = 11,
		HAND_JOINT_MIDDLE_PROXIMAL = 12,
		HAND_JOINT_MIDDLE_INTERMEDIATE = 13,
		HAND_JOINT_MIDDLE_DISTAL = 14,
		HAND_JOINT_MIDDLE_TIP = 15,
		HAND_JOINT_RING_METACARPAL = 16,
		HAND_JOINT_RING_PROXIMAL = 17,
		HAND_JOINT_RING_INTERMEDIATE = 18,
		HAND_JOINT_RING_DISTAL = 19,
		HAND_JOINT_RING_TIP = 20,
		HAND_JOINT_LITTLE_METACARPAL = 21,
		HAND_JOINT_LITTLE_PROXIMAL = 22,
		HAND_JOINT_LITTLE_INTERMEDIATE = 23,
		HAND_JOINT_LITTLE_DISTAL = 24,
		HAND_JOINT_LITTLE_TIP = 25,

		HAND_JOINT_COUNT_TOTAL,
		HAND_JOINT_COUNT = HAND_JOINT_COUNT_TOTAL - 2
	};

	struct joint_range
	{
		float min;
		float max;
	};

	struct joint_definition
	{
		XrHandJoint parent;
		std::optional<joint_range> posx;
		std::optional<joint_range> posy;
		std::optional<joint_range> posz;
		std::optional<joint_range> rotx;
		std::optional<joint_range> roty;
		std::optional<joint_range> rotz;
	};

	constexpr static std::array<joint_definition, HAND_JOINT_COUNT> joints =
	        // clang-format off
        {{
            // Thumb
            {HAND_JOINT_WRIST,               {{-0.05, 0.05}}, {{-0.05, 0.05}}, {{-0.05, 0.05}}, {{-1.2, 0.1}}, {{-1, 1}},     {{-1, 1}}},            // HAND_JOINT_THUMB_METACARPAL
            {HAND_JOINT_THUMB_METACARPAL,    {},              {},              {},              {{0, 0.2}},    {{-1.2, 1.2}}, {}},                   // HAND_JOINT_THUMB_PROXIMAL
            {HAND_JOINT_THUMB_PROXIMAL,      {},              {},              {},              {{-0.2, 0}},   {{-1, 1}},     {{-0.2, 0.2}}},        // HAND_JOINT_THUMB_DISTAL
            {HAND_JOINT_THUMB_DISTAL,        {},              {},              {},              {},            {},            {}},                   // HAND_JOINT_THUMB_TIP =

            // Index
            {HAND_JOINT_WRIST,               {{-0.05, 0.05}}, {{-0.02, 0}},    {{-0.05, 0.05}}, {},            {},            {}},                   // HAND_JOINT_INDEX_METACARPAL
            {HAND_JOINT_WRIST,               {},              {},              {},              {{-1.5, 0.3}}, {{-0.5, 0.5}}, {{-0.4, 0.4}}},        // HAND_JOINT_INDEX_PROXIMAL
            {HAND_JOINT_INDEX_PROXIMAL,      {},              {},              {},              {{-2, 0}},     {},            {}},                   // HAND_JOINT_INDEX_INTERMEDIATE
            {HAND_JOINT_INDEX_INTERMEDIATE,  {},              {},              {},              {{-1.5, 0.2}}, {},            {}},                   // HAND_JOINT_INDEX_DISTAL
            {HAND_JOINT_INDEX_DISTAL,        {},              {},              {},              {},            {},            {}},                   // HAND_JOINT_INDEX_TIP

            // Middle finger
            {HAND_JOINT_WRIST,               {{-0.01, 0.01}}, {{-0.02, 0}},    {{-0.05, 0}},    {},            {},            {}},                   // HAND_JOINT_MIDDLE_METACARPAL
            {HAND_JOINT_WRIST,               {},              {},              {},              {{-1.6, 0.2}}, {{-0.4, 0.4}}, {{-0.2, 0.2}}},        // HAND_JOINT_MIDDLE_PROXIMAL
            {HAND_JOINT_MIDDLE_PROXIMAL,     {},              {},              {},              {{-2, 0}},     {},            {}},                   // HAND_JOINT_MIDDLE_INTERMEDIATE
            {HAND_JOINT_MIDDLE_INTERMEDIATE, {},              {},              {},              {{-1.5, 0.2}}, {},            {}},                   // HAND_JOINT_MIDDLE_DISTAL
            {HAND_JOINT_MIDDLE_DISTAL,       {},              {},              {},              {},            {},            {}},                   // HAND_JOINT_MIDDLE_TIP

            // Ring finger
            {HAND_JOINT_WRIST,               {},              {},              {},              {},            {},            {}},                   // HAND_JOINT_RING_METACARPAL
            {HAND_JOINT_WRIST,               {},              {},              {},              {{-1.7, 0.2}}, {{-0.5, 0.5}}, {{-0.4, 0.4}}},        // HAND_JOINT_RING_PROXIMAL
            {HAND_JOINT_RING_PROXIMAL,       {},              {},              {},              {{-2, 0}},     {},            {}},                   // HAND_JOINT_RING_INTERMEDIATE
            {HAND_JOINT_RING_INTERMEDIATE,   {},              {},              {},              {{-1.5, 0.2}}, {},            {}},                   // HAND_JOINT_RING_DISTAL
            {HAND_JOINT_RING_DISTAL,         {},              {},              {},              {},            {},            {}},                   // HAND_JOINT_RING_TIP

            // Little finger
            {HAND_JOINT_WRIST,               {},              {},              {},              {},            {},            {}},                   // HAND_JOINT_LITTLE_METACARPAL
            {HAND_JOINT_WRIST,               {},              {},              {},              {{-1.8, 0.4}}, {{-1, 1}},     {{-1, 1}}},            // HAND_JOINT_LITTLE_PROXIMAL
            {HAND_JOINT_LITTLE_PROXIMAL,     {},              {},              {},              {{-2, 0.1}},   {},            {}},                   // HAND_JOINT_LITTLE_INTERMEDIATE
            {HAND_JOINT_LITTLE_INTERMEDIATE, {},              {},              {},              {{-1.5, 0.1}}, {},            {}},                   // HAND_JOINT_LITTLE_DISTAL
            {HAND_JOINT_LITTLE_DISTAL,       {},              {},              {},              {},            {},            {}},                   // HAND_JOINT_LITTLE_TIP
        }};
	// clang-format on

	constexpr static int nb_dof = []() {
		int N = 0;
		for (const auto & joint: joints)
		{
			if (joint.posx)
				N++;
			if (joint.posy)
				N++;
			if (joint.posz)
				N++;
			if (joint.rotx)
				N++;
			if (joint.roty)
				N++;
			if (joint.rotz)
				N++;
		}

		return N;
	}();

	constexpr static int nb_constants = HAND_JOINT_COUNT * 6 - nb_dof;

	struct joint_pose
	{
		std::array<float, 3> position;
		std::array<float, 4> rotation; // xyzw
		std::array<float, 3> linear_velocity;
		std::array<float, 3> angular_velocity;
	};

	using pose = std::array<joint_pose, HAND_JOINT_COUNT_TOTAL>;

	struct packed_pose
	{
		std::array<float, 3> wrist_position;
		std::array<uint8_t, 3> wrist_rotation;
		std::array<uint8_t, nb_dof> dofs;

		// TODO quantize
		std::array<float, 3> wrist_linear_velocity;
		std::array<float, 3> wrist_angular_velocity;
		std::array<float, nb_dof> dof_velocity;
	};

	using pose_constants = std::array<float, nb_constants>;

	// Split the pose between constants and degrees of freedom
	std::pair<pose_constants, packed_pose> pack(const pose & p);

	// Apply one step of gradient descent to minimize the position residual
	void apply_ik(const pose_constants & c, packed_pose & p, const pose & q);

	// Compute the full pose
	pose unpack(const pose_constants & c, const packed_pose & p);

	void update_suggested_range(const pose & p, std::array<joint_definition, HAND_JOINT_COUNT> & suggested_range);
};
