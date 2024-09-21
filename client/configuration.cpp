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
#include "hardware.h"
#include <fstream>
#include <magic_enum.hpp>
#include <simdjson.h>

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
		case feature::eye_gaze:
			if (not application::get_eye_gaze_supported())
				return false;
			break;
		case feature::face_tracking:
			if (not application::get_fb_face_tracking2_supported())
				return false;
			break;
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
		request_permission(permission_name(f), 0);
#endif
	features[f] = state;
}

configuration::configuration(xr::system & system)
{
	passthrough_enabled = system.passthrough_supported() == xr::system::passthrough_type::color;
	try
	{
		*this = configuration(application::get_config_path() / "client.json");
		if (system.passthrough_supported() == xr::system::passthrough_type::no_passthrough)
			passthrough_enabled = false;
	}
	catch (std::exception & e)
	{
		spdlog::warn("Cannot read configuration: {}", e.what());
	}
}

configuration::configuration(const std::string & path)
{
	simdjson::dom::parser parser;
	simdjson::dom::element root = parser.load(path);
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

	if (auto val = root["show_performance_metrics"]; val.is_bool())
		show_performance_metrics = val.get_bool();

	if (auto val = root["preferred_refresh_rate"]; val.is_double())
	{
		preferred_refresh_rate = val.get_double();
	}

	if (auto val = root["resolution_scale"]; val.is_double())
	{
		resolution_scale = val.get_double();
	}

	if (auto val = root["passthrough_enabled"]; val.is_bool())
		passthrough_enabled = val.get_bool();

	for (const auto & [i, name]: magic_enum::enum_entries<feature>())
	{
		if (auto val = root[name]; val.is_bool())
			features[i] = val.get_bool();
	}
}

static std::ostream & operator<<(std::ostream & stream, feature f)
{
	return stream << "\"" << magic_enum::enum_name(f) << "\"";
}

void configuration::save()
{
	std::stringstream ss;

	for (auto & [cookie, server_data]: servers)
	{
		if (server_data.autoconnect || server_data.manual)
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

	json << "{\"servers\":[" << servers_str << "],"
	     << "\"show_performance_metrics\":" << std::boolalpha << show_performance_metrics;
	if (preferred_refresh_rate != 0.)
		json << ",\"preferred_refresh_rate\":" << preferred_refresh_rate;
	json << ",\"resolution_scale\":" << resolution_scale;
	json << ",\"passthrough_enabled\":" << std::boolalpha << passthrough_enabled;
	for (auto & [key, value]: features)
		json << "," << key << ":" << std::boolalpha << value;
	json << "}";
}
