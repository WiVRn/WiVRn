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
#include <span>
#include <variant>
#include <vector>
#include <openxr/openxr.h>

#include "wivrn_serialization_types.h"

namespace xrt::drivers::wivrn
{

// Default port for server to listen, both TCP and UDP
static const int default_port = 9757;

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
	h265,
	hevc = h265,
};

struct audio_data
{
	uint64_t timestamp;
	std::span<uint8_t> payload;
	data_holder data;
};

namespace from_headset
{

struct headset_info_packet
{
	uint32_t recommended_eye_width;
	uint32_t recommended_eye_height;
	std::vector<float> available_refresh_rates;
	float preferred_refresh_rate;
	struct audio_description
	{
		uint8_t num_channels;
		uint32_t sample_rate;
	};
	std::optional<audio_description> speaker;
	std::optional<audio_description> microphone;
	std::array<XrFovf, 2> fov;
	bool hand_tracking;
};

struct handshake
{
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

struct hand_tracking
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
		XrPosef pose;
		XrVector3f linear_velocity;
		XrVector3f angular_velocity;
		float radius;
		uint8_t flags;
	};

	uint64_t timestamp;
	std::array<pose, XR_HAND_JOINT_COUNT_EXT> left;
	std::array<pose, XR_HAND_JOINT_COUNT_EXT> right;
};

struct inputs
{
	struct input_value
	{
		device_id id;
		float value;
		uint64_t last_change_time;
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
	uint64_t sent_to_decoder;
	uint64_t received_from_decoder;
	uint64_t blitted;
	uint64_t displayed;

	std::array<XrPosef, 2> received_pose;
	std::array<XrPosef, 2> real_pose;

	uint8_t data_packets;
	uint8_t received_data_packets;
};

using packets = std::variant<headset_info_packet, feedback, audio_data, handshake, tracking, hand_tracking, inputs, timesync_response>;
} // namespace from_headset

namespace to_headset
{

struct handshake
{
};

struct audio_stream_description
{
	struct device
	{
		uint8_t num_channels;
		uint32_t sample_rate;
	};
	std::optional<device> speaker;
	std::optional<device> microphone;
};

struct video_stream_description
{
	struct item
	{
		// useful dimensions of the video stream
		uint16_t width;
		uint16_t height;
		// dimensions of the video, may include padding at the end
		uint16_t video_width;
		uint16_t video_height;
		uint16_t offset_x;
		uint16_t offset_y;
		video_codec codec;
		std::optional<uint32_t> range;       // VkSamplerYcbcrRange
		std::optional<uint32_t> color_model; // VkSamplerYcbcrModelConversion
	};
	struct foveation_parameter_item
	{
		double center;
		double scale;
		double a;
		double b;
	};
	struct foveation_parameter
	{
		foveation_parameter_item x;
		foveation_parameter_item y;
	};
	uint16_t width;
	uint16_t height;
	float fps;
	std::array<foveation_parameter, 2> foveation;
	std::vector<item> items;
};

class video_stream_data_shard
{
public:
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

	// Position information, must be present on first video shard
	struct view_info_t
	{
		// ns in headset time referential
		uint64_t display_time;

		std::array<XrPosef, 2> pose;
		std::array<XrFovf, 2> fov;
	};
	std::optional<view_info_t> view_info;

	// Information about timing, on last video shard
	struct timing_info_t
	{
		uint64_t encode_begin;
		uint64_t send_begin;
		uint64_t send_end;
	};
	std::optional<timing_info_t> timing_info;
	// Actual video data, may contain multiple NAL units
	std::span<uint8_t> payload;

	// Container for the data, read payload instead
	data_holder data;
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

using packets = std::variant<handshake, audio_stream_description, video_stream_description, audio_data, video_stream_data_shard, haptics, timesync_query>;

} // namespace to_headset

} // namespace xrt::drivers::wivrn
