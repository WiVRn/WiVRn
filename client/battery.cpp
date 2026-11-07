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
#include <stop_token>

battery::~battery()
{
	battery_thread.request_stop();
}

void battery::start()
{
	battery_thread = std::jthread([this](std::stop_token stop) { return run_battery_poller(stop); });
}

void battery::run_battery_poller(std::stop_token stop)
{
	using namespace std::chrono_literals;

	xr::instance & instance = application::instance().xr_instance;
	const bool profile_battery_info_available = instance.has_extension(XR_EXT_INTERACTION_PROFILE_BATTERY_STATE_DISPLAY_EXTENSION_NAME);
	std::array top_level_paths{
	        std::make_pair(instance.string_to_path("/user/hand/left"), device::left_controller),
	        std::make_pair(instance.string_to_path("/user/hand/right"), device::right_controller),
	        std::make_pair(instance.string_to_path("/user/gamepad"), device::gamepad),
	};

	std::unique_lock lock(m);
	while (!stop.stop_requested())
	{
		spdlog::debug("Polling batteries");

#ifndef __ANDROID__
		{
			battery_status status = get_hmd_battery();
			if (status != statuses.at(device::head))
			{
				on_status_update(device::head, std::move(status));
			}
		}
#endif

		if (profile_battery_info_available)
		{
			for (auto [path, device]: top_level_paths)
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
					CHECK_XR(xrGetCurrentInteractionProfile(application::instance().xr_session, path, &profile_state));
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
				        .charge = std::move(charge),
				        .charging = (battery_state.stateFlags & XR_BATTERY_STATE_DISPLAY_STATE_CHARGING_BIT_EXT) != 0,
				};
				if (status != statuses.at(device))
				{
					on_status_update(device, std::move(status));
				}
			}
		}

		cv.wait_for(lock, stop, 10s, [&stop] { return stop.stop_requested(); });
	}

	spdlog::info("Battery thread exiting");
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