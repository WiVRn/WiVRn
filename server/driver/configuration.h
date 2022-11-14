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

#include <map>
#include <optional>
#include <string>

#include "wivrn_packets.h"

struct configuration
{
	struct encoder
	{
		std::string name;
		std::optional<double> width;
		std::optional<double> height;
		std::optional<double> offset_x;
		std::optional<double> offset_y;
		std::optional<int> bitrate;
		std::optional<int> group;
		std::optional<xrt::drivers::wivrn::video_codec> codec;
		std::map<std::string, std::string> options;
	};

	std::vector<encoder> encoders;
	std::optional<double> scale;

	static configuration read_user_configuration();

};
