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
#include <simdjson.h>

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

	if (auto val = root["microphone"]; val.is_bool())
		microphone = val.get_bool();

	if (auto val = root["passthrough_enabled"]; val.is_bool())
		passthrough_enabled = val.get_bool();
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
	json << ",\"microphone\":" << std::boolalpha << microphone;
	json << ",\"passthrough_enabled\":" << std::boolalpha << passthrough_enabled;
	json << "}";
}
