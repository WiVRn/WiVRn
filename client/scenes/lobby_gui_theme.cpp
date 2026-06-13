/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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

#define IMGUI_DEFINE_MATH_OPERATORS

#include "lobby.h"

#include "render/ui_theme.h"
#include "render/ui_widgets.h"
#include "utils/i18n.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace ui = wivrn::ui;

void scenes::lobby::gui_theme()
{
	ui::theme & theme = ui::current();

	ui::page_header(_S("Theme"), _S("Accent color, palette and sizing of the interface."));

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ui::metrics::card_item_spacing);
	const float control_w = ui::metrics::setting_control_width;

	ui::begin_card("##theme");
	{
		// Accent
		ImGui::TextUnformatted(_S("Accent color"));
		ImGui::Dummy({0, 2});
		for (const auto & swatch: ui::accent_swatches())
		{
			const bool selected = theme.accent.x == swatch.base.x and theme.accent.y == swatch.base.y and theme.accent.z == swatch.base.z;
			const auto flags = ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder | (selected ? 0 : ImGuiColorEditFlags_NoDragDrop);
			if (ImGui::ColorButton(swatch.name, swatch.base, flags, {ImGui::GetFrameHeight() * 1.4f, ImGui::GetFrameHeight() * 1.4f}))
				ui::set_accent(swatch);
			ImGui::SameLine();
		}
		ImGui::NewLine();

		ui::row_separator();

		// Preset
		static int preset = 1; // dark_default
		const auto preset_list = ui::presets();
		std::vector<ui::combo_item> preset_items;
		for (const auto & p: preset_list)
			preset_items.push_back({p.name.c_str()});

		static const int preset_default = 1;
		ui::setting_label(_S("Preset"), _S("Surface and background palette"), control_w);
		if (ui::combo("##preset", _("Theme preset"), preset_items, &preset, control_w, &preset_default))
		{
			// keep the accent across a preset change, only the surfaces swap
			const ImVec4 keep_accent = theme.accent;
			const ImVec4 keep_hover = theme.accent_hovered;
			const ImVec4 keep_active = theme.accent_active;
			ui::set_theme(preset_list[preset]);
			theme.accent = keep_accent;
			theme.accent_hovered = keep_hover;
			theme.accent_active = keep_active;
		}

		ui::row_separator();

		// Rounding
		static const int rounding_default = 8;
		int rounding = int(theme.rounding);
		ui::setting_label(_S("Rounding"), _S("Corner radius of controls"), control_w);
		if (ui::slider_int("##rounding", &rounding, 0, 20, "%d px", {control_w, 0}, &rounding_default))
			theme.rounding = float(rounding);

		ui::row_separator();

		static const int card_rounding_default = 14;
		int card_rounding = int(theme.card_rounding);
		ui::setting_label(_S("Card rounding"), _S("Corner radius of panels"), control_w);
		if (ui::slider_int("##card_rounding", &card_rounding, 0, 28, "%d px", {control_w, 0}, &card_rounding_default))
			theme.card_rounding = float(card_rounding);

		ui::row_separator();

		// Text size, applied globally via style.FontScaleMain next frame. 100% is the
		// design default; the value is a user multiplier on metrics::font_base.
		static const int font_scale_default = 100;
		int font_scale = int(theme.font_scale * 100);
		ui::setting_label(_S("Text size"), _S("Global font scale"), control_w);
		if (ui::slider_int("##font_scale", &font_scale, 60, 140, "%d%%", {control_w, 0}, &font_scale_default))
			theme.font_scale = float(font_scale) / 100.f;

		ui::row_separator();

		// Panel transparency
		static const int opacity_default = 100;
		int opacity = int(theme.background_alpha * 100);
		ui::setting_label(_S("Panel opacity"), _S("Opacity of the main panel background"), control_w);
		if (ui::slider_int("##opacity", &opacity, 20, 100, "%d%%", {control_w, 0}, &opacity_default))
			theme.background_alpha = float(opacity) / 100.f;

		ui::end_card();
	}

	ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
}
