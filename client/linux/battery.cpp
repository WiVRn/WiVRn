/*
 * WiVRn VR streaming
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

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace
{
std::optional<std::string> read_line(const std::filesystem::path & path)
{
	std::ifstream file(path);
	std::string line;
	if (file and std::getline(file, line))
		return line;
	return std::nullopt;
}
std::optional<float> read_percent(const std::filesystem::path & path)
{
	std::ifstream file(path);
	float percent;
	if (file >> percent and percent >= 0 and percent <= 100)
		return percent / 100;
	return std::nullopt;
}

battery::battery_status read_battery_status(const std::filesystem::path & percent_path, const std::filesystem::path & status_path)
{
	auto charge = read_percent(percent_path);
	if (not charge)
		return {};
	auto status = read_line(status_path);
	return {
	        .charge = charge,
	        .charging = status == "Charging" or status == "Full",
	};
}
} // namespace

battery::battery_status battery::get_hmd_battery()
{
	// SteamOS on the Steam Frame: the charger daemon's percentage is what the system UI shows
	const std::filesystem::path deckard = "/run/deckardcharger";
	if (auto battery = read_battery_status(deckard / "battery_percent", deckard / "battery_status"); battery.charge)
		return battery;

	// Generic Linux: the first battery in sysfs
	std::error_code ec;
	for (const auto & supply: std::filesystem::directory_iterator("/sys/class/power_supply", ec))
	{
		if (read_line(supply.path() / "type") != "Battery")
			continue;
		if (auto battery = read_battery_status(supply.path() / "capacity", supply.path() / "status"); battery.charge)
			return battery;
	}

	return {};
}
