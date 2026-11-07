/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2026  Sapphire <imsapphire0@gmail.com>
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

#include <condition_variable>
#include <magic_enum_containers.hpp>
#include <mutex>
#include <optional>
#include <thread>

class battery
{
public:
	enum device : uint8_t
	{
		head = 0,
		left_controller,
		right_controller,
		gamepad,
	};

	struct battery_status
	{
		std::optional<float> charge = std::nullopt;
		bool charging = false;

		bool operator!=(const battery_status & other) const
		{
			return charge != other.charge || charging != other.charging;
		}
	};

	~battery();

private:
	std::recursive_mutex m;
	magic_enum::containers::array<device, battery_status> statuses;

	// we need to poll for controller batteries, there's no XR event sent when their state changes
	// linux implementation also doesn't have an event-based API for battery
	std::jthread battery_thread;
	std::condition_variable_any cv;
	void run_battery_poller(std::stop_token stop);

#ifndef __ANDROID__
	battery_status get_hmd_battery();
#endif

public:
	void start();

	// updates statuses and sends the new battery status to the focused scene
	void on_status_update(device device, battery_status && new_status);

	inline battery_status get_battery_status(device device)
	{
		std::lock_guard lock(m);
		return statuses.at(device);
	};
};
