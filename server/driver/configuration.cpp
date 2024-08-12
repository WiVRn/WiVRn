/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include <filesystem>
#include <fstream>
#define JSON_DISABLE_ENUM_SERIALIZATION 1
#ifdef JSON_DIAGNOSTICS
#undef JSON_DIAGNOSTICS
#endif
#define JSON_DIAGNOSTICS 1
#include <nlohmann/json.hpp>
#include <random>
#include <stdlib.h>

#include "util/u_logging.h"

#include "configuration.h"
#include "utils/xdg_base_directory.h"

static std::filesystem::path config_file = xdg_config_home() / "wivrn" / "config.json";
static std::filesystem::path cookie_file = xdg_config_home() / "wivrn" / "cookie";

namespace xrt::drivers::wivrn
{
NLOHMANN_JSON_SERIALIZE_ENUM(
        video_codec,
        {
                {video_codec(-1), ""},
                {h264, "h264"},
                {h264, "avc"},
                {h265, "h265"},
                {h265, "hevc"},
        })
}

void configuration::set_config_file(const std::filesystem::path & path)
{
	config_file = path;
}

configuration configuration::read_user_configuration()
{
	configuration result;
	if (not std::filesystem::exists(config_file))
		return result;
	std::ifstream file(config_file);
	try
	{
		auto json = nlohmann::json::parse(file);

		if (json.contains("scale"))
		{
			if (json["scale"].is_number())
				result.scale = std::array<double, 2>{json["scale"], json["scale"]};
			else
				result.scale = json["scale"];
		}

		if (json.contains("bitrate"))
		{
			result.bitrate = json["bitrate"];
		}

		if (json.contains("encoders"))
		{
			for (const auto & encoder: json["encoders"])
			{
				configuration::encoder e;
				e.name = encoder.at("encoder");

#define SET_IF(property)                 \
	if (encoder.contains(#property)) \
		e.property = encoder[#property];

				SET_IF(width);
				SET_IF(height);
				SET_IF(offset_x);
				SET_IF(offset_y);
				SET_IF(group);
				SET_IF(codec);
				if (e.codec == xrt::drivers::wivrn::video_codec(-1))
					throw std::runtime_error("invalid codec value " + encoder["codec"].get<std::string>());
				SET_IF(options);
				SET_IF(device);
				result.encoders.push_back(e);
			}
		}

		if (json.contains("application"))
		{
			if (json["application"].is_string())
				result.application.push_back(json["application"]);
			else
			{
				for (const auto & i: json["application"])
				{
					result.application.push_back(i);
				}
			}
		}

		if (json.contains("tcp_only"))
		{
			result.tcp_only = json["tcp_only"];
		}
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Invalid configuration file: %s", e.what());
		return {};
	}

	return result;
}

std::string server_cookie()
{
	{
		std::ifstream cookie(cookie_file);
		char buffer[33];
		cookie.read(buffer, sizeof(buffer) - 1);

		if (cookie)
			return {buffer, sizeof(buffer) - 1};
	}

	{
		std::random_device r;
		std::default_random_engine engine(r());

		std::uniform_int_distribution<int> dist(0, 61);

		char buffer[33];
		for (int i = 0; i < 32; i++)
		{
			int c = dist(engine);

			if (c < 10)
				buffer[i] = '0' + c;
			else if (c < 36)
				buffer[i] = 'A' + c - 10;
			else
				buffer[i] = 'a' + c - 36;
		}

		buffer[sizeof(buffer) - 1] = 0;

		std::filesystem::create_directories(cookie_file.parent_path());
		std::ofstream cookie(cookie_file);
		cookie.write(buffer, sizeof(buffer) - 1);

		return buffer;
	}
}
