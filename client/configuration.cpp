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
#include <functional>
#include <magic_enum.hpp>
#include <string_view>
#include <type_traits>
#include <vector>

#ifdef __ANDROID__
#include "android/permissions.h"
#endif

// If no refresh rate is configured, don't select a too high one
static const float max_default_rate = 100;

// key <-> member serialization descriptor for scalar settings (see config_fields()).
// The few settings that aren't scalars are handled explicitly in save()/load().
struct config_field
{
	std::function<void(std::ostream &, const configuration &)> save;
	std::function<void(simdjson::dom::element, configuration &)> load;
};

namespace
{
template <typename>
inline constexpr bool is_optional_v = false;
template <typename T>
inline constexpr bool is_optional_v<std::optional<T>> = true;

template <typename T>
struct remove_optional
{
	using type = T;
};
template <typename T>
struct remove_optional<std::optional<T>>
{
	using type = T;
};

std::ostream & operator<<(std::ostream & stream, feature f)
{
	return stream << "\"" << magic_enum::enum_name(f) << "\"";
}

template <typename T>
void write_value(std::ostream & json, const T & value)
{
	if constexpr (std::is_same_v<T, bool>)
		json << std::boolalpha << value;
	else if constexpr (std::is_same_v<T, std::string>)
		json << json_string(value);
	else if constexpr (std::is_enum_v<T>)
		json << json_string(magic_enum::enum_name(value));
	else if constexpr (std::is_integral_v<T>)
		json << (uint64_t)value;
	else // floating point
		json << value;
}

template <typename T>
std::optional<T> read_value(simdjson::simdjson_result<simdjson::dom::element> val)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		if (val.is_bool())
			return val.get_bool();
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		if (val.is_string())
			return std::string(std::string_view(val.get_string().value()));
	}
	else if constexpr (std::is_enum_v<T>)
	{
		if (val.is_string())
		{
			auto name = val.get_string().value();
			for (const auto & [e, n]: magic_enum::enum_entries<T>())
				if (name == n)
					return e;
		}
	}
	else if constexpr (std::is_integral_v<T>)
	{
		if (val.is_uint64())
			return (T)val.get_uint64();
		if (val.is_int64() and int64_t(val.get_int64()) >= 0)
			return (T)val.get_int64();
	}
	else // floating point
	{
		if (val.is_double())
			return (T)val.get_double();
		if (val.is_int64())
			return (T)val.get_int64();
		if (val.is_uint64())
			return (T)val.get_uint64();
	}
	return std::nullopt;
}

// build a field for a plain member: bool / float / integral / std::string / enum,
// optionally wrapped in std::optional (only written when set)
template <typename T>
config_field scalar(const char * key, T configuration::* member)
{
	return {
	        .save = [key, member](std::ostream & json, const configuration & c) {
		        if constexpr (is_optional_v<T>)
		        {
			        if (c.*member)
			        {
				        json << ",\"" << key << "\":";
				        write_value(json, *(c.*member));
			        }
		        }
		        else
		        {
			        json << ",\"" << key << "\":";
			        write_value(json, c.*member);
		        } },
	        .load = [key, member](simdjson::dom::element root, configuration & c) {
		        if (auto v = read_value<typename remove_optional<T>::type>(root[key]))
			        c.*member = *v; },
	};
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
	const float default_refresh_rate = preferred_refresh_rate;

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

		for (const auto & f: config_fields())
			f.load(root, *this);

		// refresh rate must be supported by this headset, otherwise fall back to the default
		if (preferred_refresh_rate != 0 and not utils::contains(rates, preferred_refresh_rate))
			preferred_refresh_rate = default_refresh_rate;

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

		// nested object
		if (auto val = root["openxr_post_processing"]; val.is_object())
		{
			auto obj = val.get_object();
			openxr_post_processing = {};
			if (auto v = obj["super_sampling"]; v.is_int64())
				openxr_post_processing.super_sampling = v.get_int64();
			if (auto v = obj["sharpening"]; v.is_int64())
				openxr_post_processing.sharpening = v.get_int64();
		}

		// feature flags, keyed by enum name
		for (const auto & [f, name]: magic_enum::enum_entries<feature>())
			if (auto val = root[name]; val.is_bool())
				features[f] = val.get_bool();

		if (system.passthrough_supported() == xr::passthrough_type::none)
			passthrough_enabled = false;
	}
	catch (std::exception & e)
	{
		spdlog::warn("Cannot read configuration: {}", e.what());
	}
}

const std::vector<config_field> & configuration::config_fields()
{
	static const std::vector<config_field> fields = {
	        scalar("resolution_scale", &configuration::resolution_scale),
	        scalar("bitrate_bps", &configuration::bitrate_bps),
	        scalar("enable_stream_gui", &configuration::enable_stream_gui),
	        scalar("app_list_view", &configuration::app_list_view),
	        scalar("app_icon_size", &configuration::app_icon_size),
	        scalar("theme_preset", &configuration::theme_preset),
	        scalar("theme_accent", &configuration::theme_accent),
	        scalar("theme_rounding", &configuration::theme_rounding),
	        scalar("theme_card_rounding", &configuration::theme_card_rounding),
	        scalar("theme_font_scale", &configuration::theme_font_scale),
	        scalar("theme_background_alpha", &configuration::theme_background_alpha),
	        scalar("passthrough_enabled", &configuration::passthrough_enabled),
	        scalar("mic_unprocessed_audio", &configuration::mic_unprocessed_audio),
	        scalar("forward_keyboard", &configuration::forward_keyboard),
	        scalar("forward_mouse", &configuration::forward_mouse),
	        scalar("forward_gamepad", &configuration::forward_gamepad),
	        scalar("virtual_keyboard_layout", &configuration::virtual_keyboard_layout),
	        scalar("override_foveation_enable", &configuration::override_foveation_enable),
	        scalar("override_foveation_pitch", &configuration::override_foveation_pitch),
	        scalar("override_foveation_distance", &configuration::override_foveation_distance),
	        scalar("first_run", &configuration::first_run),
	        scalar("locale", &configuration::locale),
	        scalar("environment_model", &configuration::environment_model),
	        scalar("high_power_mode", &configuration::high_power_mode),
	        scalar("fps_divider", &configuration::fps_divider),
	        scalar("extended_config", &configuration::extended_config),
	        scalar("preferred_refresh_rate", &configuration::preferred_refresh_rate),
	        scalar("minimum_refresh_rate", &configuration::minimum_refresh_rate),
	        scalar("stream_scale", &configuration::stream_scale),
	        scalar("codec", &configuration::codec),
	        scalar("bit_depth", &configuration::bit_depth),
	};
	return fields;
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

	for (const auto & f: config_fields())
		f.save(json, *this);

	// nested object
	json << ",\"openxr_post_processing\":{\"super_sampling\":" << openxr_post_processing.super_sampling;
	json << ",\"sharpening\":" << openxr_post_processing.sharpening << "}";

	// feature flags, keyed by enum name
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
