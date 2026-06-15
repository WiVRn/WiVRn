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

#include "configuration.h"
#include "application.h"
#include "utils/contains.h"
#include "utils/json_string.h"
#include "wivrn_packets.h"

#include <fstream>
#include <magic_enum.hpp>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#ifdef __ANDROID__
#include "android/permissions.h"
#endif

// If no refresh rate is configured, don't select a too high one
static const float max_default_rate = 100;

namespace
{
// scalar settings with a mechanical key <-> member mapping
// everything else is handled explicitly in the constructor and save() below
using config_member = std::variant<
        bool configuration::*,
        float configuration::*,
        uint32_t configuration::*,
        std::string configuration::*>;

struct config_field
{
	const char * key;
	config_member member;
};

const std::vector<config_field> & scalar_fields()
{
	static const std::vector<config_field> fields = {
	        {"resolution_scale", &configuration::resolution_scale},
	        {"bitrate_bps", &configuration::bitrate_bps},
	        {"enable_stream_gui", &configuration::enable_stream_gui},
	        {"app_list_view", &configuration::app_list_view},
	        {"app_icon_size", &configuration::app_icon_size},
	        {"theme_preset", &configuration::theme_preset},
	        {"theme_accent", &configuration::theme_accent},
	        {"theme_rounding", &configuration::theme_rounding},
	        {"theme_card_rounding", &configuration::theme_card_rounding},
	        {"theme_font_scale", &configuration::theme_font_scale},
	        {"theme_background_alpha", &configuration::theme_background_alpha},
	        {"passthrough_enabled", &configuration::passthrough_enabled},
	        {"mic_unprocessed_audio", &configuration::mic_unprocessed_audio},
	        {"forward_keyboard", &configuration::forward_keyboard},
	        {"forward_mouse", &configuration::forward_mouse},
	        {"forward_gamepad", &configuration::forward_gamepad},
	        {"virtual_keyboard_layout", &configuration::virtual_keyboard_layout},
	        {"override_foveation_enable", &configuration::override_foveation_enable},
	        {"override_foveation_pitch", &configuration::override_foveation_pitch},
	        {"override_foveation_distance", &configuration::override_foveation_distance},
	        {"first_run", &configuration::first_run},
	        {"locale", &configuration::locale},
	        {"environment_model", &configuration::environment_model},
	        {"high_power_mode", &configuration::high_power_mode},
	        {"fps_divider", &configuration::fps_divider},
	        {"extended_config", &configuration::extended_config},
	};
	return fields;
}

void load_field(simdjson::dom::element root, configuration & c, const config_field & f)
{
	auto val = root[f.key];
	std::visit([&](auto mp) {
		using T = std::remove_cvref_t<decltype(c.*mp)>;
		if constexpr (std::is_same_v<T, bool>)
		{
			if (val.is_bool())
				c.*mp = val.get_bool();
		}
		else if constexpr (std::is_same_v<T, std::string>)
		{
			if (val.is_string())
				c.*mp = std::string(std::string_view(val.get_string().value()));
		}
		else if constexpr (std::is_same_v<T, uint32_t>)
		{
			if (val.is_uint64())
				c.*mp = uint32_t(uint64_t(val.get_uint64()));
			else if (val.is_int64() and int64_t(val.get_int64()) >= 0)
				c.*mp = uint32_t(int64_t(val.get_int64()));
		}
		else // float
		{
			if (val.is_double())
				c.*mp = float(double(val.get_double()));
			else if (val.is_int64())
				c.*mp = float(int64_t(val.get_int64()));
			else if (val.is_uint64())
				c.*mp = float(uint64_t(val.get_uint64()));
		}
	},
	           f.member);
}

void save_field(std::ostream & json, const configuration & c, const config_field & f)
{
	std::visit([&](auto mp) {
		using T = std::remove_cvref_t<decltype(c.*mp)>;
		json << ",\"" << f.key << "\":";
		if constexpr (std::is_same_v<T, bool>)
			json << std::boolalpha << (c.*mp);
		else if constexpr (std::is_same_v<T, std::string>)
			json << json_string(c.*mp);
		else
			json << (c.*mp);
	},
	           f.member);
}
} // namespace

bool configuration::check_feature(feature f) const
{
	{
		std::lock_guard lock(mutex);

		auto & system = application::get_system();

		auto it = features.find(f);
		if (it == features.end())
			return false;

		// Skip permission checks if not requested
		if (not it->second)
			return false;
		switch (f)
		{
			case feature::microphone:
				break;
			case feature::hand_tracking:
				if (not system.hand_tracking_supported())
					return false;
				break;
			case feature::eye_gaze:
				if (not application::get_eye_gaze_supported())
					return false;
				break;
			case feature::face_tracking:
				if (system.face_tracker_supported() == xr::face_tracker_type::none)
					return false;
				break;
			case feature::body_tracking:
				if (system.body_tracker_supported() == xr::body_tracker_type::none)
					return false;
				break;
		}
	}
#ifdef __ANDROID__
	return check_permission(application::get_hmd_traits().permission_name(f));
#else
	return true;
#endif
}

void configuration::set_feature(feature f, bool state)
{
#ifdef __ANDROID__
	if (state)
	{
		request_permission(application::get_hmd_traits().permission_name(f), [this, f](bool granted) {
			{
				std::lock_guard lock(mutex);
				features[f] = granted;
			}
			save();
		});

		return;
	}
#endif
	{
		std::lock_guard lock(mutex);
		features[f] = state;
	}
	save();
}

configuration::configuration(xr::system & system, xr::session & session)
{
	passthrough_enabled = system.passthrough_supported() == xr::passthrough_type::color;
	features[feature::hand_tracking] = system.hand_tracking_supported();

	const auto & rates = session.get_refresh_rates();
	for (auto rate: rates)
	{
		if (rate >= max_default_rate)
			break;
		preferred_refresh_rate = rate;
	}

	try
	{
		simdjson::dom::parser parser;
		simdjson::dom::element root = parser.load((application::get_config_path() / "client.json").native());
		for (simdjson::dom::object i: simdjson::dom::array(root["servers"]))
		{
			server_data data{
			        .autoconnect = i["autoconnect"].get_bool(),
			        .manual = i["manual"].get_bool(),
			        .visible = false,
			        .compatible = true,
			        .service = {
			                .name = (std::string)i["pretty_name"],
			                .hostname = (std::string)i["hostname"],
			                .port = (int)i["port"].get_int64(),
			                .tcp_only = i["tcp_only"].is_bool() && i["tcp_only"].get_bool(),
			                .txt = {{"cookie", (std::string)i["cookie"]}}

			        }};
			servers.emplace(data.service.txt["cookie"], data);
		}

		for (const auto & f: scalar_fields())
			load_field(root, *this, f);

		// settings with non-mechanical encoding
		if (auto val = root["preferred_refresh_rate"]; val.is_double())
		{
			float f = val.get_double();
			if (f == 0 or utils::contains(rates, f))
				preferred_refresh_rate = f;
		}

		if (auto val = root["minimum_refresh_rate"]; val.is_double())
			minimum_refresh_rate = val.get_double();

		if (auto val = root["stream_scale"]; val.is_double())
			stream_scale = val.get_double();

		if (auto val = root["codec"]; val.is_string())
		{
			const auto codec_str = val.get_string().value();
			for (const auto & [c, name]: magic_enum::enum_entries<wivrn::video_codec>())
			{
				if (codec_str == name)
				{
					codec = c;
				}
			}
		}

		if (auto val = root["bit_depth"]; val.is_uint64())
			bit_depth = val.get_uint64();

		if (auto val = root["openxr_post_processing"]; val.is_object())
			parse_openxr_post_processing_options(val.get_object());

		if (auto val = root["body_parts"]; val.is_object())
		{
			for (const auto & [b, name]: magic_enum::enum_entries<wivrn::from_headset::body_part_mask>())
			{
				const auto & body_part = val[name];
				if (!body_part.is_bool())
					continue;

				if (body_part.get_bool())
				{
					body_part_mask |= std::to_underlying(b);
				}
				else
				{
					body_part_mask &= ~std::to_underlying(b);
				}
			}
		}

		for (const auto & [i, name]: magic_enum::enum_entries<feature>())
		{
			if (auto val = root[name]; val.is_bool())
				features[i] = val.get_bool();
		}

		if (system.passthrough_supported() == xr::passthrough_type::none)
			passthrough_enabled = false;
	}
	catch (std::exception & e)
	{
		spdlog::warn("Cannot read configuration: {}", e.what());
	}
}

void configuration::parse_openxr_post_processing_options(simdjson::simdjson_result<simdjson::dom::object> openxr_post_processing_root)
{
	openxr_post_processing = {};
	if (auto val = openxr_post_processing_root["super_sampling"]; val.is_int64())
		openxr_post_processing.super_sampling = val.get_int64();

	if (auto val = openxr_post_processing_root["sharpening"]; val.is_int64())
		openxr_post_processing.sharpening = val.get_int64();
}

static std::ostream & operator<<(std::ostream & stream, feature f)
{
	return stream << "\"" << magic_enum::enum_name(f) << "\"";
}

static std::ostream & write_openxr_post_processing(std::ofstream & stream, const configuration::openxr_post_processing_settings & openxr_post_processing)
{
	stream << "{\"super_sampling\":" << openxr_post_processing.super_sampling;
	stream << ",\"sharpening\":" << openxr_post_processing.sharpening;
	stream << "}";
	return stream;
}

void configuration::save()
{
	std::lock_guard lock(mutex);

	std::stringstream ss;

	for (auto & [cookie, server_data]: servers)
	{
		if (server_data.autoconnect or server_data.manual)
		{
			ss << "{";
			ss << "\"autoconnect\":" << std::boolalpha << server_data.autoconnect << ",";
			ss << "\"manual\":" << std::boolalpha << server_data.manual << ",";
			ss << "\"pretty_name\":" << json_string(server_data.service.name) << ",";
			ss << "\"hostname\":" << json_string(server_data.service.hostname) << ",";
			ss << "\"port\":" << server_data.service.port << ",";
			ss << "\"tcp_only\":" << std::boolalpha << server_data.service.tcp_only << ",";
			ss << "\"cookie\":" << json_string(cookie);
			ss << "},";
		}
	}

	std::string servers_str = ss.str();
	if (servers_str != "")
		servers_str.pop_back(); // Remove last comma

	std::ofstream json(application::get_config_path() / "client.json");

	json << "{\"servers\":[" << servers_str << "]";

	for (const auto & f: scalar_fields())
		save_field(json, *this, f);

	// settings with non-mechanical encoding
	json << ",\"preferred_refresh_rate\":" << preferred_refresh_rate;
	if (minimum_refresh_rate)
		json << ",\"minimum_refresh_rate\":" << *minimum_refresh_rate;
	if (stream_scale)
		json << ",\"stream_scale\":" << *stream_scale;
	if (codec)
		json << ",\"codec\":" << json_string(magic_enum::enum_name(*codec));
	json << ",\"bit_depth\":" << (uint64_t)bit_depth;
	json << ",\"openxr_post_processing\":";
	write_openxr_post_processing(json, openxr_post_processing);

	std::stringstream body_part_ss;
	for (const auto & [b, name]: magic_enum::enum_entries<wivrn::from_headset::body_part_mask>())
	{
		bool enabled = (body_part_mask & std::to_underlying(b)) != 0;
		body_part_ss << "\"" << name << "\":" << std::boolalpha << enabled << ",";
	}

	std::string body_parts_str = body_part_ss.str();
	if (!body_parts_str.empty())
	{
		body_parts_str.pop_back(); // Remove last comma
		json << ",\"body_parts\":{" << body_parts_str << "}";
	}

	for (auto & [key, value]: features)
		json << "," << key << ":" << std::boolalpha << value;
	json << "}";
}

void configuration::set_stream_scale(float val)
{
	stream_scale = val;
}

float configuration::get_stream_scale() const
{
	if (stream_scale)
		return *stream_scale;
	if (check_feature(feature::eye_gaze))
		return 0.3;
	return 0.5;
}
