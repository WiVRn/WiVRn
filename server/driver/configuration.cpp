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

#include <filesystem>
#include <fstream>
#define JSON_DISABLE_ENUM_SERIALIZATION 1
#define JSON_DIAGNOSTICS 1
#include <nlohmann/json.hpp>
#include <stdlib.h>

#include "util/u_logging.h"

#include "configuration.h"

static std::filesystem::path get_config_base_dir()
{
	const char * xdg_config_home = std::getenv("XDG_CONFIG_HOME");
	if (xdg_config_home)
		return xdg_config_home;
	const char * home = std::getenv("HOME");
	if (home)
		return std::filesystem::path(home) / ".config";
	return ".";
}

static std::filesystem::path get_config_file()
{
	return get_config_base_dir() / "wivrn" / "config.json";
}

namespace xrt::drivers::wivrn
{

NLOHMANN_JSON_SERIALIZE_ENUM(video_codec, {
                                                  {video_codec(-1), ""},
                                                  {h264, "h264"},
                                                  {h264, "avc"},
                                                  {h265, "h265"},
                                                  {h265, "hevc"},
                                          })
}

configuration configuration::read_user_configuration()
{
	configuration result;
	auto config_file_path = get_config_file();
	if (not std::filesystem::exists(config_file_path))
		return result;
	std::ifstream file(config_file_path);
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
				SET_IF(bitrate);
				SET_IF(group);
				SET_IF(codec);
				if (e.codec == xrt::drivers::wivrn::video_codec(-1))
					throw std::runtime_error("invalid codec value " + encoder["codec"].get<std::string>());
				SET_IF(options);
				result.encoders.push_back(e);
			}
		}
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Invalid configuration file: %s", e.what());
		return {};
	}

	return result;
}
