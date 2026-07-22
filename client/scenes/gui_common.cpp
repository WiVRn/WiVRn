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
#include "application.h"
#include "configuration.h"
#include "constants.h"
#include "render/imgui_impl.h"
#include "render/ui_theme.h"
#include "render/ui_widgets.h"
#include "utils/i18n.h"

#include "wivrn_packets.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/system.h"

#include "imgui.h"
#include <IconsFontAwesome6.h>
#include <cmath>

namespace wivrn::gui
{
float toggle_width()
{
	return ImGui::GetFrameHeight() * ui::metrics::control_height * ui::metrics::toggle_aspect;
}

bool body_tracking_parts(
        xr::system & system,
        imgui_context & imgui_ctx,
        configuration & config,
        bool in_game)
{
	auto body_tracker = system.body_tracker_supported();
	if (body_tracker == xr::body_tracker_type::none or body_tracker == xr::body_tracker_type::htc)
		return false;

	const std::array body_parts{
	        std::make_tuple(from_headset::body_part_mask::chest, "##chest", _C("virtual body tracker selection", "Chest")),
	        std::make_tuple(from_headset::body_part_mask::left_elbow, "##left_elbow", _C("virtual body tracker selection", "Left elbow")),
	        std::make_tuple(from_headset::body_part_mask::right_elbow, "##right_elbow", _C("virtual body tracker selection", "Right elbow")),
	        std::make_tuple(from_headset::body_part_mask::hip, "##hip", _C("virtual body tracker selection", "Hip")),
	        std::make_tuple(from_headset::body_part_mask::left_knee, "##left_knee", _C("virtual body tracker selection", "Left knee")),
	        std::make_tuple(from_headset::body_part_mask::right_knee, "##right_knee", _C("virtual body tracker selection", "Right knee")),
	        std::make_tuple(from_headset::body_part_mask::left_foot, "##left_foot", _C("virtual body tracker selection", "Left foot")),
	        std::make_tuple(from_headset::body_part_mask::right_foot, "##right_foot", _C("virtual body tracker selection", "Right foot")),
	};

	bool changed = false;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 10});
	ui::begin_card("##body_tracking_parts");

	ImGui::BeginDisabled(in_game);
	for (const auto & [bit, id, name]: body_parts)
	{
		if (body_tracker == xr::body_tracker_type::fb and bit > from_headset::body_part_mask::hip)
			continue;

		const auto underlying = std::to_underlying(bit);
		bool enabled = config.body_part_mask & underlying;

		const float label_bottom = ui::setting_label(name, "", toggle_width());
		if (ui::toggle(id, &enabled))
		{
			if (enabled)
				config.body_part_mask |= underlying;
			else
				config.body_part_mask &= ~underlying;

			config.save();
			changed = true;
		}

		// keep the row tall enough for a multi-line description and add breathing room
		// below. Reserve with Dummy (SetCursorPos would not grow the content size).
		const float pad = std::max(label_bottom - ImGui::GetCursorPosY(), 0.f) + ui::metrics::label_bottom_pad;
		ImGui::Dummy({0, pad});
	}
	ImGui::EndDisabled();

	ui::end_card();
	ImGui::PopStyleVar();

	return changed;
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
