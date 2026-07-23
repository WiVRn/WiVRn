/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2026  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "gui_common.h"

#include "android/battery.h"
#include "render/ui_theme.h"
#include "render/ui_widgets.h"

#include "imgui.h"
#include <IconsFontAwesome6.h>
#include <cmath>
#include <spdlog/fmt/fmt.h>

namespace wivrn::gui
{
float toggle_width()
{
	return ImGui::GetFrameHeight() * ui::metrics::control_height * ui::metrics::toggle_aspect;
}

std::optional<battery_indicator> battery_status_indicator(XrTime now)
{
#ifdef __ANDROID__
	const auto battery = get_battery_status();
	if (not battery.charge)
		return std::nullopt;

	const char * icon = ICON_FA_BATTERY_FULL;
	int icon_nr;
	if (battery.charging)
		icon_nr = *battery.charge > 0.995 ? 5 : now / 500'000'000 % 5;
	else
		icon_nr = std::round((*battery.charge) * 4);
	switch (icon_nr)
	{
		case 0:
			icon = ICON_FA_BATTERY_EMPTY;
			break;
		case 1:
			icon = ICON_FA_BATTERY_QUARTER;
			break;
		case 2:
			icon = ICON_FA_BATTERY_HALF;
			break;
		case 3:
			icon = ICON_FA_BATTERY_THREE_QUARTERS;
			break;
		case 4:
			icon = ICON_FA_BATTERY_FULL;
			break;
		case 5:
			icon = ICON_FA_PLUG;
			break;
	}

	ui::chip_style style;
	if (*battery.charge < 0.2)
		style = ui::chip_style::danger;
	else if (*battery.charge < 0.5)
		style = ui::chip_style::warning;
	else
		style = ui::chip_style::success;

	return battery_indicator{
	        .label = fmt::format("{} {}%", icon, (int)std::round(*battery.charge * 100)),
	        .style = style,
	};
#else
	(void)now;
	return std::nullopt;
#endif
}
} // namespace wivrn::gui
