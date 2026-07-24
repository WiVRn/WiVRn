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

#include "wivrn_discover.h"
#include "wivrn_packets.h"

#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <simdjson.h>
#include <string>
#include <type_traits>
#include <openxr/openxr.h>

namespace xr
{
class session;
class system;
} // namespace xr

enum class feature
{
	microphone,
	hand_tracking,
	eye_gaze,
	face_tracking,
	body_tracking,
};

class configuration
{
public:
	struct server_data
	{
		bool autoconnect;
		bool manual;
		bool visible;
		bool compatible;

		wivrn_discover::service service;
	};

	std::map<std::string, server_data> servers;
	float preferred_refresh_rate = 0;
	std::optional<float> minimum_refresh_rate;
	float resolution_scale = 1.0;
	std::optional<wivrn::video_codec> codec;
	uint32_t bitrate_bps = 50'000'000;
	uint8_t bit_depth = 10;

	bool passthrough_enabled = false;
	bool mic_unprocessed_audio = false;

	// Input forwarding, per device. Off by default; only effective if the server permits it.
	bool forward_keyboard = false;
	bool forward_mouse = false;
	bool forward_gamepad = false;

	std::underlying_type_t<wivrn::from_headset::body_part_mask> body_part_mask = ~0;

	// Client-side chroma key passthrough: keys out HSV range from decoded
	// stream so the headset's passthrough (alpha blend env / FB / HTC) shows
	// through. Applies to any running application, regardless of whether it
	// outputs alpha.
	struct chroma_key_settings
	{
		bool enabled = false;
		// HSV in [0, 1]; hsv_min.h > hsv_max.h wraps around the hue ring.
		std::array<float, 3> hsv_min{0.25f, 0.30f, 0.15f};
		std::array<float, 3> hsv_max{0.45f, 1.00f, 1.00f};
		// Soft falloff width around the HSV range, in normalized HSV units.
		float curve = 0.10f;
		// How aggressively to subtract the key hue from surviving pixels.
		float despill = 0.50f;
	};
	chroma_key_settings chroma_key;

	// Sunglasses: blends a solid colour over the decoded stream to dim it.
	// Only affects the stream layer, passthrough regions keep their real
	// brightness.
	struct sunglasses_settings
	{
		bool enabled = false;
		// HSV in [0, 1]; default black = pure dimming.
		std::array<float, 3> hsv{0.f, 0.f, 0.f};
		// Blend strength: 0 = invisible, 1 = fully opaque tint.
		float alpha = 0.3f;
	};
	sunglasses_settings sunglasses;

	bool enable_stream_gui = true;

	// XR_FB_composition_layer_settings extension flags
	struct openxr_post_processing_settings
	{
		XrCompositionLayerSettingsFlagsFB super_sampling = 0;
		XrCompositionLayerSettingsFlagsFB sharpening = 0;
	};
	openxr_post_processing_settings openxr_post_processing{};

	std::string virtual_keyboard_layout = "QWERTY";

	std::string environment_model = "assets://ground.glb";

	bool override_foveation_enable = false;
	float override_foveation_pitch = -10 * M_PI / 180;
	float override_foveation_distance = 3;

	bool high_power_mode = true;
	uint32_t fps_divider = 1;

	// Allow unsafe config values
	bool extended_config = false;

	bool first_run = true;

	std::string locale;

	bool check_feature(feature f) const;
	void set_feature(feature f, bool state);

private:
	mutable std::mutex mutex;
	std::map<feature, bool> features;
	std::optional<float> stream_scale;

	void parse_openxr_post_processing_options(simdjson::simdjson_result<simdjson::dom::object> root);

public:
	configuration(xr::system &, xr::session &);
	configuration() = default;

	void save();

	void set_stream_scale(float);
	float get_stream_scale() const;

	uint32_t max_bitrate(bool extended) const
	{
		return extended ? 800'000'000u : 200'000'000u;
	}

	uint32_t max_bitrate() const
	{
		return max_bitrate(extended_config);
	}
};
