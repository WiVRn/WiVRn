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
	float resolution_scale = 1.4;
	bool passthrough_enabled = false;
	bool mic_unprocessed_audio = false;

	bool fb_lower_body = false;
	bool fb_hip = true;

	bool enable_stream_gui = true;

	// Snapdragon Game Super Resolution
	struct sgsr_settings
	{
		bool enabled = false;
		float upscaling_factor = 1.5;
		bool use_edge_direction = true;
		float edge_threshold = 4.0;
		float edge_sharpness = 2.0;
	};
	sgsr_settings sgsr{};

	// XR_FB_composition_layer_settings extension flags
	struct openxr_post_processing_settings
	{
		XrCompositionLayerSettingsFlagsFB super_sampling = 0;
		XrCompositionLayerSettingsFlagsFB sharpening = 0;
	};
	openxr_post_processing_settings openxr_post_processing{};

	std::string virtual_keyboard_layout = "QWERTY";

	bool override_foveation_enable = false;
	float override_foveation_pitch = 10 * M_PI / 180;
	float override_foveation_distance = 3;

	bool first_run = true;

	bool check_feature(feature f) const;
	void set_feature(feature f, bool state);

private:
	mutable std::mutex mutex;
	std::map<feature, bool> features;

	void parse_sgsr_options(simdjson::simdjson_result<simdjson::dom::object> root);
	void parse_openxr_post_processing_options(simdjson::simdjson_result<simdjson::dom::object> root);

public:
	configuration(xr::system &);
	configuration() = default;

	void save();
};
