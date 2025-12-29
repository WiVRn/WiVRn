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

#include "hardware.h"
#include "wivrn_discover.h"
#include "wivrn_packets.h"

#include <map>
#include <mutex>
#include <optional>
#include <simdjson.h>
#include <string>

namespace xr
{
class system;
}

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
	std::optional<float> preferred_refresh_rate;
	std::optional<float> minimum_refresh_rate;
	float resolution_scale = 1.0;
	std::optional<wivrn::video_codec> codec;
	uint32_t bitrate_bps = 50'000'000;
	uint8_t bit_depth = 10;

	bool passthrough_enabled = false;
	bool mic_unprocessed_audio = false;

	bool fb_lower_body = false;
	bool fb_hip = true;

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
	float override_foveation_pitch = 10 * M_PI / 180;
	float override_foveation_distance = 3;

	bool high_power_mode;

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
	configuration(xr::system &);
	configuration() = default;

	void save();

	void set_stream_scale(float);
	float get_stream_scale() const;

	uint32_t max_bitrate() const
	{
		return extended_config ? 800'000'000u : 200'000'000u;
	}
};
