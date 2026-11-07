/*
 * WiVRn VR streaming
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

#include "battery.h"
#include "application.h"
#include "scene.h"
#include <chrono>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>

battery::battery(xr::instance & instance, xr::session & session) :
        session(session)
{
	if (instance.has_extension(XR_EXT_INTERACTION_PROFILE_BATTERY_STATE_DISPLAY_EXTENSION_NAME))
	{
		top_level_paths.emplace({
		        std::make_pair(instance.string_to_path("/user/hand/left"), device::left_controller),
		        std::make_pair(instance.string_to_path("/user/hand/right"), device::right_controller),
		        std::make_pair(instance.string_to_path("/user/gamepad"), device::gamepad),
		});
	}
}

void battery::poll_batteries()
{
	using namespace std::chrono_literals;

	auto now = std::chrono::steady_clock::now();
	if (now - last_poll < 10s)
		return;
	last_poll = now;

	spdlog::debug("Polling batteries");

	std::lock_guard lock(m);
#ifndef __ANDROID__
	{
		battery_status status = get_hmd_battery();
		if (status != statuses.at(device::head))
		{
			on_status_update(device::head, std::move(status));
		}
	}
#endif

	if (top_level_paths)
	{
		for (auto [path, device]: *top_level_paths)
		{
			XrBatteryStateDisplayEXT battery_state{
			        .type = XR_TYPE_BATTERY_STATE_DISPLAY_EXT,
			};
			XrInteractionProfileState profile_state{
			        .type = XR_TYPE_INTERACTION_PROFILE_STATE,
			        .next = &battery_state,
			};

			try
			{
				CHECK_XR(xrGetCurrentInteractionProfile(session, path, &profile_state));
			}
			catch (std::exception & e)
			{
				spdlog::warn("Failed to get battery charge state for device {}: {}", magic_enum::enum_name(device), e.what());
				continue;
			}

			std::optional<float> charge{};
			if (battery_state.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_VALID_BIT_EXT)
				charge = battery_state.batteryLevel;

			battery_status status{
			        .charge = charge,
			        .charging = (battery_state.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_CHARGING_BIT_EXT) != 0,
			};
			if (status != statuses.at(device))
			{
				on_status_update(device, std::move(status));
			}
		}
	}
}

void battery::on_status_update(battery::device device, battery::battery_status && new_status)
{
	std::lock_guard lock(m);

	if (std::shared_ptr<scene> s = application::instance().current_scene())
	{
		s->on_battery_update(device, new_status);
	}
	statuses.at(device) = std::move(new_status);
}