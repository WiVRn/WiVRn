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

#include <fstream>
#include <magic_enum.hpp>

#ifdef __ANDROID__
#include "android/permissions.h"
#endif

static std::string json_string(const std::string & in)
{
	std::string out;
	out.reserve(in.size() + 2);

	out += '"';

	for (char c: in)
	{
		switch (c)
		{
			case '\b':
				out += "\\b";
				break;

			case '\f':
				out += "\\f";
				break;

			case '\n':
				out += "\\n";
				break;

			case '\r':
				out += "\\r";
				break;

			case '\t':
				out += "\\t";
				break;

			case '"':
				out += "\\\"";
				break;

			case '\\':
				out += "\\\\";
				break;

			default:
				out += c;
				break;
		}
	}

	out += '"';

	return out;
}

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
	return check_permission(permission_name(f));
#else
	return true;
#endif
}

void configuration::set_feature(feature f, bool state)
{
#ifdef __ANDROID__
	if (state)
	{
		request_permission(permission_name(f), [this, f](bool granted) {
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

configuration::configuration(xr::system & system)
{
	passthrough_enabled = system.passthrough_supported() == xr::passthrough_type::color;
	features[feature::hand_tracking] = system.hand_tracking_supported();
	try
	{
		simdjson::dom::parser parser;
		simdjson::dom::element root = parser.load(application::get_config_path() / "client.json");
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

		if (auto val = root["preferred_refresh_rate"]; val.is_double())
			preferred_refresh_rate = val.get_double();

		if (auto val = root["minimum_refresh_rate"]; val.is_double())
			minimum_refresh_rate = val.get_double();

		if (auto val = root["resolution_scale"]; val.is_double())
			resolution_scale = val.get_double();

		if (auto val = root["enable_stream_gui"]; val.is_bool())
			enable_stream_gui = val.get_bool();

		if (auto val = root["sgsr"]; val.is_object())
			parse_sgsr_options(val.get_object());

		if (auto val = root["openxr_post_processing"]; val.is_object())
			parse_openxr_post_processing_options(val.get_object());

		if (auto val = root["passthrough_enabled"]; val.is_bool())
			passthrough_enabled = val.get_bool();

		if (auto val = root["mic_unprocessed_audio"]; val.is_bool())
			mic_unprocessed_audio = val.get_bool();

		if (auto val = root["fb_lower_body"]; val.is_bool())
			fb_lower_body = val.get_bool();
		if (auto val = root["fb_hip"]; val.is_bool())
			fb_hip = val.get_bool();

		if (auto val = root["virtual_keyboard_layout"]; val.is_string())
			virtual_keyboard_layout = val.get_string().value();

		for (const auto & [i, name]: magic_enum::enum_entries<feature>())
		{
			if (auto val = root[name]; val.is_bool())
				features[i] = val.get_bool();
		}

		if (system.passthrough_supported() == xr::passthrough_type::none)
			passthrough_enabled = false;

		if (auto val = root["override_foveation_enable"]; val.is_bool())
			override_foveation_enable = val.get_bool();

		if (auto val = root["override_foveation_pitch"]; val.is_double())
			override_foveation_pitch = val.get_double();

		if (auto val = root["override_foveation_distance"]; val.is_double())
			override_foveation_distance = val.get_double();

		if (auto val = root["first_run"]; val.is_bool())
			first_run = val.get_bool();
	}
	catch (std::exception & e)
	{
		spdlog::warn("Cannot read configuration: {}", e.what());

		// Restore default configuration
		servers.clear();
		preferred_refresh_rate.reset();
		minimum_refresh_rate.reset();
		resolution_scale = 1.4;
		sgsr = {};
		openxr_post_processing = {};
		passthrough_enabled = system.passthrough_supported() == xr::passthrough_type::color;
	}
}

void configuration::parse_sgsr_options(simdjson::simdjson_result<simdjson::dom::object> sgsr_root)
{
	sgsr = {};
	if (auto val = sgsr_root["enabled"]; val.is_bool())
		sgsr.enabled = val.get_bool();

	if (auto val = sgsr_root["upscaling_factor"]; val.is_double())
		sgsr.upscaling_factor = val.get_double();

	if (auto val = sgsr_root["use_edge_direction"]; val.is_bool())
		sgsr.use_edge_direction = val.get_bool();

	if (auto val = sgsr_root["edge_threshold"]; val.is_double())
		sgsr.edge_threshold = val.get_double();

	if (auto val = sgsr_root["edge_sharpness"]; val.is_double())
		sgsr.edge_sharpness = val.get_double();
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

static std::ostream & write_sgsr(std::ofstream & stream, const configuration::sgsr_settings & sgsr)
{
	stream << "{\"enabled\":" << std::boolalpha << sgsr.enabled;
	stream << ",\"upscaling_factor\":" << sgsr.upscaling_factor;
	stream << ",\"use_edge_direction\":" << std::boolalpha << sgsr.use_edge_direction;
	stream << ",\"edge_threshold\":" << sgsr.edge_threshold;
	stream << ",\"edge_sharpness\":" << sgsr.edge_sharpness;
	stream << "}";
	return stream;
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
	if (preferred_refresh_rate)
		json << ",\"preferred_refresh_rate\":" << *preferred_refresh_rate;
	if (minimum_refresh_rate)
		json << ",\"minimum_refresh_rate\":" << *minimum_refresh_rate;
	json << ",\"resolution_scale\":" << resolution_scale;
	json << ",\"sgsr\":";
	write_sgsr(json, sgsr);
	json << ",\"openxr_post_processing\":";
	write_openxr_post_processing(json, openxr_post_processing);
	json << ",\"passthrough_enabled\":" << std::boolalpha << passthrough_enabled;
	json << ",\"mic_unprocessed_audio\":" << std::boolalpha << mic_unprocessed_audio;
	json << ",\"fb_lower_body\":" << std::boolalpha << fb_lower_body;
	json << ",\"fb_hip\":" << std::boolalpha << fb_hip;
	json << ",\"enable_stream_gui\":" << std::boolalpha << enable_stream_gui;
	for (auto & [key, value]: features)
		json << "," << key << ":" << std::boolalpha << value;
	json << ",\"virtual_keyboard_layout\":" << json_string(virtual_keyboard_layout);
	json << ",\"override_foveation_enable\":" << std::boolalpha << override_foveation_enable;
	json << ",\"override_foveation_pitch\":" << override_foveation_pitch;
	json << ",\"override_foveation_distance\":" << override_foveation_distance;
	json << ",\"first_run\":" << std::boolalpha << first_run;
	json << "}";
}
