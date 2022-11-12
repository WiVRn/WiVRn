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

#include <array>
#include <chrono>
#include <cstdint>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <openxr/openxr.h>

namespace xrt::drivers::wivrn
{

static const int control_port = 9757;
static const int stream_port = 9757;

enum class device_id : uint8_t
{
	HEAD,                    // /user/head
	LEFT_CONTROLLER_HAPTIC,  // /user/hand/left/output/haptic
	RIGHT_CONTROLLER_HAPTIC, // /user/hand/right/output/haptic
	LEFT_GRIP,               // /user/hand/left/input/grip/pose
	LEFT_AIM,                // /user/hand/left/input/aim/pose
	RIGHT_GRIP,              // /user/hand/right/input/grip/pose
	RIGHT_AIM,               // /user/hand/right/input/aim/pose
	X_CLICK,                 // /user/hand/left/input/x/click
	X_TOUCH,                 // /user/hand/left/input/x/touch
	Y_CLICK,                 // /user/hand/left/input/y/click
	Y_TOUCH,                 // /user/hand/left/input/y/touch
	MENU_CLICK,              // /user/hand/left/input/menu/click
	LEFT_SQUEEZE_VALUE,      // /user/hand/left/input/squeeze/value
	LEFT_TRIGGER_VALUE,      // /user/hand/left/input/trigger/value
	LEFT_TRIGGER_TOUCH,      // /user/hand/left/input/trigger/touch
	LEFT_THUMBSTICK_X,       // /user/hand/left/input/thumbstick/x
	LEFT_THUMBSTICK_Y,       // /user/hand/left/input/thumbstick/y
	LEFT_THUMBSTICK_CLICK,   // /user/hand/left/input/thumbstick/click
	LEFT_THUMBSTICK_TOUCH,   // /user/hand/left/input/thumbstick/touch
	LEFT_THUMBREST_TOUCH,    // /user/hand/left/input/thumbrest/touch
	A_CLICK,                 // /user/hand/right/input/a/click
	A_TOUCH,                 // /user/hand/right/input/a/touch
	B_CLICK,                 // /user/hand/right/input/b/click
	B_TOUCH,                 // /user/hand/right/input/b/touch
	SYSTEM_CLICK,            // /user/hand/right/input/system/click
	RIGHT_SQUEEZE_VALUE,     // /user/hand/right/input/squeeze/value
	RIGHT_TRIGGER_VALUE,     // /user/hand/right/input/trigger/value
	RIGHT_TRIGGER_TOUCH,     // /user/hand/right/input/trigger/touch
	RIGHT_THUMBSTICK_X,      // /user/hand/right/input/thumbstick/x
	RIGHT_THUMBSTICK_Y,      // /user/hand/right/input/thumbstick/y
	RIGHT_THUMBSTICK_CLICK,  // /user/hand/right/input/thumbstick/click
	RIGHT_THUMBSTICK_TOUCH,  // /user/hand/right/input/thumbstick/touch
	RIGHT_THUMBREST_TOUCH,   // /user/hand/right/input/thumbrest/touch
};

enum video_codec
{
	h264,
	hevc,
	h265 = hevc,
};

namespace from_headset
{
struct headset_info_packet
{
	uint32_t recommended_eye_width;
	uint32_t recommended_eye_height;
	std::vector<float> available_refresh_rates;
	float preferred_refresh_rate;
	uint32_t microphone_sample_rate;
};

struct tracking
{
	enum flags : uint8_t
	{
		orientation_valid = 1 << 0,
		position_valid = 1 << 1,
		linear_velocity_valid = 1 << 2,
		angular_velocity_valid = 1 << 3,
		orientation_tracked = 1 << 4,
		position_tracked = 1 << 5
	};

	struct pose
	{
		device_id device;
		XrPosef pose;
		XrVector3f linear_velocity;
		XrVector3f angular_velocity;
		uint8_t flags;
	};

	struct view
	{
		// Relative to XR_REFERENCE_SPACE_TYPE_VIEW
		XrPosef pose;
		XrFovf fov;
	};

	uint64_t timestamp;
	XrViewStateFlags flags;

	std::array<view, 2> views;
	std::vector<pose> device_poses;
};

struct inputs
{
	struct input_value
	{
		device_id id;
		float value;
	};
	std::vector<input_value> values;
};

struct timesync_response
{
	std::chrono::nanoseconds query;
	uint64_t response;
};

struct feedback
{
	uint64_t frame_index;
	uint8_t stream_index;

	// Timestamps
	uint64_t received_first_packet;
	uint64_t received_last_packet;
	uint64_t reconstructed;
	uint64_t sent_to_decoder;
	uint64_t received_from_decoder;
	uint64_t blitted;
	uint64_t displayed;

	std::array<XrPosef, 2> received_pose;
	std::array<XrPosef, 2> real_pose;

	uint8_t data_packets;
	uint8_t parity_packets;
	uint8_t received_data_packets;
	uint8_t received_parity_packets;
};

using control_packets = std::variant<headset_info_packet, feedback>;
using stream_packets = std::variant<tracking, inputs, timesync_response>;
} // namespace from_headset

namespace to_headset
{

struct video_stream_description
{
	struct item
	{
		uint16_t width;
		uint16_t height;
		uint16_t offset_x;
		uint16_t offset_y;
		video_codec codec;
	};
	uint16_t width;
	uint16_t height;
	float fps;
	std::vector<item> items;
};

struct video_stream_data_shard
{
	inline static const size_t max_payload_size = 1400;
	enum flags : uint8_t
	{
		start_of_slice = 1,
		end_of_slice = 1 << 1,
		end_of_frame = 1 << 2,
	};
	// Identifier of stream in video_stream_description
	uint8_t stream_item_idx;
	// Counter increased for each frame
	uint64_t frame_idx;
	// Identifier of the shard within the frame
	uint16_t shard_idx;
	uint8_t flags;

	// Actual video data, may contain multiple NAL units
	std::vector<uint8_t> payload;

	// Position information, must be present on last video shard
	struct view_info_t
	{
		// ns in headset time referential
		uint64_t display_time;

		std::array<XrPosef, 2> pose;
		std::array<XrFovf, 2> fov;
	};
	std::optional<view_info_t> view_info;
};

struct video_stream_parity_shard
{
	uint8_t stream_item_idx;
	uint64_t frame_idx;
	uint16_t data_shard_count;
	uint8_t num_parity_elements;
	uint8_t parity_element;
	// Parity data
	std::vector<uint8_t> payload;
};

struct haptics
{
	device_id id;
	std::chrono::nanoseconds duration;
	float frequency;
	float amplitude;
};

struct timesync_query
{
	std::chrono::nanoseconds query;
};

using stream_packets = std::variant<video_stream_data_shard, video_stream_parity_shard, haptics, timesync_query>;
using control_packets = std::variant<video_stream_description>;

} // namespace to_headset

} // namespace xrt::drivers::wivrn
