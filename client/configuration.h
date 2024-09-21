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
	float preferred_refresh_rate = 0;
	float resolution_scale = 1.4;
	bool show_performance_metrics = false;
	bool passthrough_enabled = false;

	bool check_feature(feature f) const;
	void set_feature(feature f, bool state);

private:
	std::map<feature, bool> features;
	configuration(const std::string &);

public:
	configuration(xr::system &);

	void save();
};
