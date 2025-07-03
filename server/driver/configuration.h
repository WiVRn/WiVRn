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

#pragma once

#include <array>
#include <chrono>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>

#include "wivrn_packets.h"

namespace wivrn
{

enum class service_publication
{
	none,
	avahi,
};

struct configuration
{
	struct encoder
	{
		std::string name;
		std::optional<double> width;
		std::optional<double> height;
		std::optional<double> offset_x;
		std::optional<double> offset_y;
		std::optional<int> group;
		std::optional<wivrn::video_codec> codec;
		std::map<std::string, std::string> options;
		std::optional<std::string> device;
	};

	std::vector<encoder> encoders;
	std::optional<encoder> encoder_passthrough;
	std::optional<int> bitrate;
	std::optional<std::array<double, 2>> scale;
	std::optional<std::array<float, 3>> grip_surface;
	std::vector<std::string> application;
	bool debug_gui = false;
	bool use_steamvr_lh = false;
	bool tcp_only = false;
	service_publication publication = service_publication::avahi;

	// monostate: default value, string: user defined, nullptr: disabled
	std::variant<std::monostate, std::string, std::nullptr_t> openvr_compat_path;

	static void set_config_file(const std::filesystem::path &);
	static std::filesystem::path get_config_file();

	static nlohmann::json read_configuration();
	configuration();
};

std::string server_cookie();

struct headset_key
{
	std::string public_key;
	std::string name;
	std::optional<std::chrono::system_clock::time_point> last_connection;
};

std::vector<headset_key> known_keys();
void add_known_key(headset_key key);
void remove_known_key(const std::string & key);
void rename_known_key(headset_key key);
void update_last_connection_timestamp(const std::string & key);

} // namespace wivrn
