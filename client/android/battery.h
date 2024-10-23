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

#include <chrono>
#include <memory>
#include <optional>

class battery
{
public:
	struct status
	{
		std::optional<float> charge = std::nullopt;
		bool charging = false;
	};

private:
	struct pimpl;
	std::unique_ptr<pimpl> p;

	const std::chrono::seconds battery_check_interval = std::chrono::seconds{2};

	std::chrono::steady_clock::time_point next_battery_check = std::chrono::steady_clock::now();
	status last_status;

public:
	battery();
	~battery();

	status get();
};
