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
#include <openssl/aes.h>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <openxr/openxr.h>

#include "smp.h"
#include "wivrn_serialization_types.h"
#include "xr/pico_eye_types.h"

namespace wivrn
{

// Default port for server to listen, both TCP and UDP
static const int default_port = 9757;

static constexpr int protocol_revision = 2;

enum class device_id : uint8_t
{
	HEAD,                    // /user/head
	LEFT_CONTROLLER_HAPTIC,  // /user/hand/left/output/haptic
	RIGHT_CONTROLLER_HAPTIC, // /user/hand/right/output/haptic
	LEFT_TRIGGER_HAPTIC,     // /user/hand/left/output/haptic_trigger
	RIGHT_TRIGGER_HAPTIC,    // /user/hand/right/output/haptic_trigger
	LEFT_THUMB_HAPTIC,       // /user/hand/left/output/haptic_thumb
	RIGHT_THUMB_HAPTIC,      // /user/hand/right/output/haptic_thumb
	LEFT_GRIP,               // /user/hand/left/input/grip/pose
	LEFT_AIM,                // /user/hand/left/input/aim/pose
	LEFT_PALM,               // /user/hand/left/palm_ext/pose
	RIGHT_GRIP,              // /user/hand/right/input/grip/pose
	RIGHT_AIM,               // /user/hand/right/input/aim/pose
	RIGHT_PALM,              // /user/hand/right/palm_ext/pose
	X_CLICK,                 // /user/hand/left/input/x/click
	X_TOUCH,                 // /user/hand/left/input/x/touch
	Y_CLICK,                 // /user/hand/left/input/y/click
	Y_TOUCH,                 // /user/hand/left/input/y/touch
	MENU_CLICK,              // /user/hand/left/input/menu/click
	LEFT_SQUEEZE_CLICK,      // /user/hand/left/input/squeeze/click
	LEFT_SQUEEZE_FORCE,      // /user/hand/left/input/squeeze/force
	LEFT_SQUEEZE_VALUE,      // /user/hand/left/input/squeeze/value
	LEFT_TRIGGER_CLICK,      // /user/hand/left/input/trigger/click
	LEFT_TRIGGER_VALUE,      // /user/hand/left/input/trigger/value
	LEFT_TRIGGER_TOUCH,      // /user/hand/left/input/trigger/touch
	LEFT_TRIGGER_PROXIMITY,  // /user/hand/left/input/trigger/proximity
	LEFT_TRIGGER_CURL,       // /user/hand/left/input/trigger/curl_fb
	LEFT_TRIGGER_SLIDE,      // /user/hand/left/input/trigger/slide_fb
	LEFT_TRIGGER_FORCE,      // /user/hand/left/input/trigger/force
	LEFT_THUMBSTICK_X,       // /user/hand/left/input/thumbstick/x
	LEFT_THUMBSTICK_Y,       // /user/hand/left/input/thumbstick/y
	LEFT_THUMBSTICK_CLICK,   // /user/hand/left/input/thumbstick/click
	LEFT_THUMBSTICK_TOUCH,   // /user/hand/left/input/thumbstick/touch
	LEFT_THUMBREST_TOUCH,    // /user/hand/left/input/thumbrest/touch
	LEFT_THUMBREST_FORCE,    // /user/hand/left/input/thumbrest/force
	LEFT_THUMB_PROXIMITY,    // /user/hand/left/input/thumb_resting_surfaces/proximity
	LEFT_TRACKPAD_X,         // /user/hand/left/input/trackpad/x
	LEFT_TRACKPAD_Y,         // /user/hand/left/input/trackpad/y
	LEFT_TRACKPAD_CLICK,     // /user/hand/left/input/trackpad/click
	LEFT_TRACKPAD_TOUCH,     // /user/hand/left/input/trackpad/touch
	LEFT_TRACKPAD_FORCE,     // /user/hand/left/input/trackpad/force
	LEFT_STYLUS_FORCE,       // /user/hand/left/input/stylus_fb/force
	A_CLICK,                 // /user/hand/right/input/a/click
	A_TOUCH,                 // /user/hand/right/input/a/touch
	B_CLICK,                 // /user/hand/right/input/b/click
	B_TOUCH,                 // /user/hand/right/input/b/touch
	SYSTEM_CLICK,            // /user/hand/right/input/system/click
	RIGHT_SQUEEZE_CLICK,     // /user/hand/right/input/squeeze/click
	RIGHT_SQUEEZE_FORCE,     // /user/hand/right/input/squeeze/force
	RIGHT_SQUEEZE_VALUE,     // /user/hand/right/input/squeeze/value
	RIGHT_TRIGGER_CLICK,     // /user/hand/right/input/trigger/click
	RIGHT_TRIGGER_VALUE,     // /user/hand/right/input/trigger/value
	RIGHT_TRIGGER_TOUCH,     // /user/hand/right/input/trigger/touch
	RIGHT_TRIGGER_PROXIMITY, // /user/hand/right/input/trigger/proximity
	RIGHT_TRIGGER_CURL,      // /user/hand/right/input/trigger/curl_fb
	RIGHT_TRIGGER_SLIDE,     // /user/hand/right/input/trigger/slide_fb
	RIGHT_TRIGGER_FORCE,     // /user/hand/right/input/trigger/force
	RIGHT_THUMBSTICK_X,      // /user/hand/right/input/thumbstick/x
	RIGHT_THUMBSTICK_Y,      // /user/hand/right/input/thumbstick/y
	RIGHT_THUMBSTICK_CLICK,  // /user/hand/right/input/thumbstick/click
	RIGHT_THUMBSTICK_TOUCH,  // /user/hand/right/input/thumbstick/touch
	RIGHT_THUMBREST_TOUCH,   // /user/hand/right/input/thumbrest/touch
	RIGHT_THUMBREST_FORCE,   // /user/hand/right/input/thumbrest/force
	RIGHT_THUMB_PROXIMITY,   // /user/hand/right/input/thumb_resting_surfaces/proximity
	RIGHT_TRACKPAD_X,        // /user/hand/right/input/trackpad/x
	RIGHT_TRACKPAD_Y,        // /user/hand/right/input/trackpad/y
	RIGHT_TRACKPAD_CLICK,    // /user/hand/right/input/trackpad/click
	RIGHT_TRACKPAD_TOUCH,    // /user/hand/right/input/trackpad/touch
	RIGHT_TRACKPAD_FORCE,    // /user/hand/right/input/trackpad/force
	RIGHT_STYLUS_FORCE,      // /user/hand/right/input/stylus_fb/force
	EYE_GAZE,                // /user/eyes_ext/input/gaze_ext/pose
};

enum class interaction_profile : uint8_t
{
	none,
	khr_simple_controller,
	bytedance_pico_neo3_controller,
	bytedance_pico4_controller,
	bytedance_pico4s_controller,
	bytedance_pico_g3_controller,
	google_daydream_controller,
	hp_mixed_reality_controller,
	htc_vive_controller,
	htc_vive_cosmos_controller,
	htc_vive_focus3_controller,
	htc_vive_pro,
	ml_ml2_controller,
	microsoft_motion_controller,
	microsoft_xbox_controller,
	oculus_go_controller,
	oculus_touch_controller,
	meta_touch_pro_controller,
	meta_touch_plus_controller,
	meta_touch_controller_rift_cv1,
	meta_touch_controller_quest_1_rift_s,
	meta_touch_controller_quest_2,
	samsung_odyssey_controller,
	valve_index_controller,
};

enum video_codec
{
	h264,
	h265,
	hevc = h265,
	av1,
};

struct audio_data
{
	XrTime timestamp;
	std::span<uint8_t> payload;
	data_holder data;
};

namespace from_headset
{
struct crypto_handshake
{
	std::string public_key; // In PEM format
	std::string name;
};

struct pin_check_1
{
	crypto::smp::msg1 message;
};

struct pin_check_3
{
	crypto::smp::msg3 message;
};

struct visibility_mask_changed
{
	struct mask
	{
		std::vector<XrVector2f> vertices;
		std::vector<uint32_t> indices;
	};
	static const int num_types = 3; // XrVisibilityMaskTypeKHR values
	using masks = std::array<mask, num_types>;

	masks data;
	uint8_t view_index;
};

enum face_type : uint8_t
{
	none,
	fb2,
	htc,
	pico,
};

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
	bool eye_gaze;
	bool palm_pose;
	bool passthrough;
	face_type face_tracking;
	uint32_t num_generic_trackers;
	std::vector<video_codec> supported_codecs; // from preferred to least preferred
	std::string system_name;
};

struct handshake
{
	// Sending this on TCP means connection will be TCP only
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

	enum state_flags : uint8_t
	{
		recentered = 1 << 0,
	};

	struct pose
	{
		XrPosef pose;
		XrVector3f linear_velocity;
		XrVector3f angular_velocity;
		device_id device;
		uint8_t flags;
	};

	struct view
	{
		// Relative to XR_REFERENCE_SPACE_TYPE_VIEW
		XrPosef pose;
		XrFovf fov;
	};

	XrTime production_timestamp;
	XrTime timestamp;
	XrViewStateFlags view_flags;

	uint8_t state_flags;

	std::array<view, 2> views;
	std::vector<pose> device_poses;

	struct fb_face2
	{
		std::array<float, XR_FACE_EXPRESSION2_COUNT_FB> weights;
		std::array<float, XR_FACE_CONFIDENCE2_COUNT_FB> confidences;
		bool is_valid;
		bool is_eye_following_blendshapes_valid;
	};

	struct htc_face
	{
		std::array<float, XR_FACIAL_EXPRESSION_EYE_COUNT_HTC> eye;
		std::array<float, XR_FACIAL_EXPRESSION_LIP_COUNT_HTC> lip;
		bool eye_active;
		bool lip_active;
	};

	std::variant<std::monostate, fb_face2, htc_face> face;
};

struct trackings
{
	// /user/hand/left and /user/hand/right
	std::array<interaction_profile, 2> interaction_profiles;
	std::vector<tracking> items;
};

struct derived_pose
{
	device_id source;
	device_id target;
	XrPosef relation;
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
	enum hand_id : uint8_t
	{
		left,
		right,
	};
	struct pose
	{
		XrPosef pose;
		XrVector3f linear_velocity;
		XrVector3f angular_velocity;
		// In order to avoid packet fragmentation
		// use 2 less bytes for radius
		uint16_t radius; // 10th of mm
		uint8_t flags;
	};

	XrTime production_timestamp;
	XrTime timestamp;
	hand_id hand;
	std::optional<std::array<pose, XR_HAND_JOINT_COUNT_EXT>> joints;
};

struct body_tracking
{
	inline static const size_t max_tracked_poses = 16;
	enum flags : uint8_t
	{
		orientation_valid = 1 << 0,
		position_valid = 1 << 1,
		orientation_tracked = 1 << 2,
		position_tracked = 1 << 3,
	};
	struct pose
	{
		XrPosef pose{};
		// maybe add velocity?
		uint8_t flags{0};
	};

	XrTime production_timestamp;
	XrTime timestamp;
	std::optional<std::array<pose, max_tracked_poses>> poses;
};

struct inputs
{
	struct input_value
	{
		device_id id;
		float value;
		XrTime last_change_time;
	};
	std::vector<input_value> values;
};

struct timesync_response
{
	XrTime query;
	XrTime response;
};

struct feedback
{
	uint64_t frame_index;
	uint8_t stream_index;

	// Timestamps
	XrTime encode_begin;
	XrTime encode_end;
	XrTime send_begin;
	XrTime send_end;
	XrTime received_first_packet;
	XrTime received_last_packet;
	XrTime sent_to_decoder;
	XrTime received_from_decoder;
	XrTime blitted;
	XrTime displayed;

	uint8_t times_displayed;
};

struct battery
{
	float charge;
	bool present;
	bool charging;
};

struct refresh_rate_changed
{
	float from;
	float to;
};

using packets = std::variant<
        crypto_handshake,
        pin_check_1,
        pin_check_3,
        headset_info_packet,
        feedback,
        audio_data,
        handshake,
        tracking,
        trackings,
        derived_pose,
        hand_tracking,
        body_tracking,
        inputs,
        timesync_response,
        battery,
        visibility_mask_changed,
        refresh_rate_changed>;
} // namespace from_headset

namespace to_headset
{

struct crypto_handshake
{
	enum class crypto_state : uint8_t
	{
		encryption_disabled,
		pin_needed,
		client_already_paired,
		pairing_disabled,
	};

	std::string public_key; // In PEM format
	crypto_state state;
};

struct pin_check_2
{
	crypto::smp::msg2 message;
};

struct pin_check_4
{
	crypto::smp::msg4 message;
};

struct handshake
{
	// -1 if stream socket should not be used
	int stream_port;
};

struct foveation_parameter
{
	// The number of source pixels for each ratio,
	// the middle one is 1:1
	//
	// how to read it:
	// 1, 4, 5, 3, 1 means:
	// the first output pixel has 3 source pixels
	// the 4 that come after have 2 source pixels
	// then 5 with 1 source pixel
	// 3 with 2 source pixels
	// 1 with 3 source pixels
	std::vector<uint16_t> x;
	std::vector<uint16_t> y;
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
	enum class channels_t
	{
		colour,
		alpha,
	};
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
		channels_t channels;
		uint8_t subsampling; // applies to width/height only, offsets are in full size pixels
		std::optional<VkSamplerYcbcrRange> range;
		std::optional<VkSamplerYcbcrModelConversion> color_model;
	};
	uint16_t width;
	uint16_t height;
	float fps;
	uint16_t defoveated_width;
	uint16_t defoveated_height;
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
		XrTime display_time;

		std::array<XrPosef, 2> pose;
		std::array<XrFovf, 2> fov;
		std::array<foveation_parameter, 2> foveation;
		// True when the frame contains an alpha channel
		bool alpha;
	};
	std::optional<view_info_t> view_info;

	// Information about timing, on last video shard
	struct timing_info_t
	{
		XrTime encode_begin;
		XrTime encode_end;
		XrTime send_begin;
		XrTime send_end;
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
	XrTime query;
};

struct tracking_control
{
	enum class id
	{
		left_aim,
		left_grip,
		left_palm,
		right_aim,
		right_grip,
		right_palm,
		left_hand,
		right_hand,
		face,
		generic_tracker,
		battery,
		microphone,

		last = microphone,
	};
	std::chrono::nanoseconds min_offset;
	std::chrono::nanoseconds max_offset;
	std::array<bool, size_t(id::last) + 1> enabled;
};

struct refresh_rate_change
{
	float fps;
};

using packets = std::variant<
        crypto_handshake,
        pin_check_2,
        pin_check_4,
        handshake,
        audio_stream_description,
        video_stream_description,
        audio_data,
        video_stream_data_shard,
        haptics,
        timesync_query,
        tracking_control,
        refresh_rate_change>;
} // namespace to_headset
} // namespace wivrn
