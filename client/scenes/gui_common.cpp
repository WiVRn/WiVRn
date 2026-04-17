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

#include "configuration.h"
#include "render/imgui_impl.h"
#include "utils/i18n.h"

#include "xr/instance.h"
#include "xr/session.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace wivrn::gui
{

std::string rate_label(float rate, uint32_t divider)
{
	if (rate == 0)
		return _C("automatic refresh rate", "Automatic");
	if (divider == 1)
		return fmt::format("{}", rate);

	return fmt::format(_cF("refresh rate selection, with reprojection", "{} ({} with space warp)"), rate / divider, rate);
}

bool refresh_rate(
        xr::instance & instance,
        xr::session & session,
        imgui_context & imgui_ctx,
        configuration & config)
{
	if (not instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
		return false;

	const auto & refresh_rates = session.get_refresh_rates();
	if (refresh_rates.empty())
		return false;

	bool changed = false;
	if (ImGui::BeginCombo(
	            _S("Refresh rate"),
	            rate_label(config.preferred_refresh_rate, config.fps_divider).c_str()))
	{
		if (ImGui::Selectable(
		            _cS("automatic refresh rate", "Automatic"),
		            config.preferred_refresh_rate == 0,
		            ImGuiSelectableFlags_SelectOnRelease))
		{
			config.preferred_refresh_rate = 0;
			config.fps_divider = 1;
			config.save();
			changed = true;
		}
		if (ImGui::IsItemHovered())
			imgui_ctx.tooltip(_("Select refresh rate based on measured application performance.\n"
			                    "May cause flicker when a change happens."));

		for (uint32_t divider: {1, 2})
		{
			for (float rate: refresh_rates)
			{
				if (ImGui::Selectable(
				            rate_label(rate, divider).c_str(),
				            rate == config.preferred_refresh_rate and divider == config.fps_divider,
				            ImGuiSelectableFlags_SelectOnRelease))
				{
					session.set_refresh_rate(rate);
					config.preferred_refresh_rate = rate;
					config.fps_divider = divider;
					config.save();
					changed = true;
				}
			}
		}
		ImGui::EndCombo();
	}
	imgui_ctx.vibrate_on_hover();

	if (config.preferred_refresh_rate == 0 and refresh_rates.size() > 2)
	{
		float min_rate = config.minimum_refresh_rate.value_or(refresh_rates.front());
		if (ImGui::BeginCombo(_S("Minimum refresh rate"), fmt::format("{}", min_rate).c_str()))
		{
			for (float rate: refresh_rates | std::views::take(refresh_rates.size() - 1))
			{
				if (ImGui::Selectable(fmt::format("{}", rate).c_str(), rate == config.minimum_refresh_rate, ImGuiSelectableFlags_SelectOnRelease))
				{
					config.minimum_refresh_rate = rate;
					config.save();
				}
			}
			ImGui::EndCombo();
		}
		imgui_ctx.vibrate_on_hover();
	}
	return changed;
}
} // namespace wivrn::gui
