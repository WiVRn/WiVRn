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

#include "application.h"
#include "configuration.h"
#include "render/ui_theme.h"
#include "render/ui_widgets.h"
#include "utils/i18n.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace ui = wivrn::ui;

void scenes::lobby::apply_theme_settings()
{
	auto & config = application::get_config();

	// preset by name; surfaces only, the accent is applied separately below
	for (const auto & p: ui::presets())
	{
		if (p.name == config.theme_preset)
		{
			ui::set_theme(p);
			break;
		}
	}

	for (const auto & swatch: ui::accent_swatches())
	{
		if (swatch.name == config.theme_accent)
		{
			ui::set_accent(swatch);
			break;
		}
	}

	ui::theme & theme = ui::current();
	theme.rounding = config.theme_rounding;
	theme.card_rounding = config.theme_card_rounding;
	theme.font_scale = config.theme_font_scale;
	ui::background_alpha() = config.theme_background_alpha;
}

void scenes::lobby::gui_theme()
{
	ui::theme & theme = ui::current();
	auto & config = application::get_config();

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
			{
				ui::set_accent(swatch);
				config.theme_accent = swatch.name;
				config.save();
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();

		ui::row_separator();

		// Preset
		const auto preset_list = ui::presets();
		std::vector<ui::combo_item> preset_items;
		for (const auto & p: preset_list)
			preset_items.push_back({p.name.c_str()});

		// seed the selection from the saved preset so the box matches what is applied
		static int preset = [&] {
			for (int i = 0; i < int(preset_list.size()); ++i)
				if (preset_list[i].name == config.theme_preset)
					return i;
			return 1; // dark_default
		}();

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
			config.theme_preset = preset_list[preset].name;
			config.save();
		}

		ui::row_separator();

		// Rounding
		static const int rounding_default = 8;
		int rounding = int(theme.rounding);
		ui::setting_label(_S("Rounding"), _S("Corner radius of controls"), control_w);
		if (ui::slider_int("##rounding", &rounding, 0, 20, "%d px", {control_w, 0}, &rounding_default))
		{
			theme.rounding = float(rounding);
			config.theme_rounding = theme.rounding;
			config.save();
		}

		ui::row_separator();

		static const int card_rounding_default = 14;
		int card_rounding = int(theme.card_rounding);
		ui::setting_label(_S("Card rounding"), _S("Corner radius of panels"), control_w);
		if (ui::slider_int("##card_rounding", &card_rounding, 0, 28, "%d px", {control_w, 0}, &card_rounding_default))
		{
			theme.card_rounding = float(card_rounding);
			config.theme_card_rounding = theme.card_rounding;
			config.save();
		}

		ui::row_separator();

		// Text size, applied globally via style.FontScaleMain next frame. 100% is the
		// design default; the value is a user multiplier on metrics::font_base.
		static const int font_scale_default = 100;
		int font_scale = int(theme.font_scale * 100);
		ui::setting_label(_S("Text size"), _S("Global font scale"), control_w);
		if (ui::slider_int("##font_scale", &font_scale, 60, 140, "%d%%", {control_w, 0}, &font_scale_default))
		{
			theme.font_scale = float(font_scale) / 100.f;
			config.theme_font_scale = theme.font_scale;
			config.save();
		}

		ui::row_separator();

		// Panel transparency, independent of the selected preset
		static const int opacity_default = 90;
		int opacity = int(ui::background_alpha() * 100);
		ui::setting_label(_S("Panel opacity"), _S("Opacity of the panel and card backgrounds"), control_w);
		if (ui::slider_int("##opacity", &opacity, 20, 100, "%d%%", {control_w, 0}, &opacity_default))
		{
			ui::background_alpha() = float(opacity) / 100.f;
			config.theme_background_alpha = ui::background_alpha();
			config.save();
		}

		ui::end_card();
	}

	ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
}
