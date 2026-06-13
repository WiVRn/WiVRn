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

#include "imgui.h"

#include "IconsFontAwesome6.h"

namespace ui = wivrn::ui;

// Card title shown above the rows
static void card_title(const char * title)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ui::current().text_muted);
	ImGui::TextUnformatted(title);
	ImGui::PopStyleColor();
	ImGui::Dummy({0, 4});
}

void scenes::lobby::gui_components()
{
	ui::theme & theme = ui::current();

	ui::page_header("Components", "Live preview of the WiVRn UI kit");

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 10});
	const float control_w = 480;

	// Theme
	ui::begin_card("##theme");
	{
		card_title("THEME");

		// Accent
		ImGui::TextUnformatted("Accent color");
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
		ui::setting_label("Preset", "Surface and background palette", control_w);
		if (ui::combo("##preset", "Theme preset", preset_items, &preset, control_w, &preset_default))
		{
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
		ui::setting_label("Rounding", "Corner radius of controls", control_w);
		if (ui::slider_int("##rounding", &rounding, 0, 20, "%d px", {control_w, 0}, &rounding_default))
			theme.rounding = float(rounding);

		ui::row_separator();

		static const int card_rounding_default = 14;
		int card_rounding = int(theme.card_rounding);
		ui::setting_label("Card rounding", "Corner radius of panels", control_w);
		if (ui::slider_int("##card_rounding", &card_rounding, 0, 28, "%d px", {control_w, 0}, &card_rounding_default))
			theme.card_rounding = float(card_rounding);

		ui::row_separator();

		// Text size, applied globally via style.FontScaleMain next frame. 100% is the
		// design default; the value is a user multiplier on metrics::font_base.
		static const int font_scale_default = 100;
		int font_scale = int(theme.font_scale * 100);
		ui::setting_label("Text size", "Global font scale", control_w);
		if (ui::slider_int("##font_scale", &font_scale, 60, 140, "%d%%", {control_w, 0}, &font_scale_default))
			theme.font_scale = float(font_scale) / 100.f;

		ui::row_separator();

		// Panel transparency
		static const int opacity_default = 88;
		int opacity = int(theme.background_alpha * 100);
		ui::setting_label("Panel opacity", "Opacity of the main panel background", control_w);
		if (ui::slider_int("##opacity", &opacity, 20, 100, "%d%%", {control_w, 0}, &opacity_default))
			theme.background_alpha = float(opacity) / 100.f;

		ui::end_card();
	}

	// Buttons
	ui::begin_card("##buttons");
	{
		card_title("BUTTONS");

		ui::button("Primary", ui::button_style::primary);
		ImGui::SameLine();
		ui::button("Secondary", ui::button_style::secondary);
		ImGui::SameLine();
		ui::button(ICON_FA_POWER_OFF "  Disconnect", ui::button_style::danger);
		ImGui::SameLine();
		ui::button("Ghost", ui::button_style::ghost);

		ui::row_separator();

		card_title("ICON BUTTONS");
		static bool mic = true;
		if (ui::icon_button(ICON_FA_MICROPHONE, {}, mic, mic ? "Mute microphone" : "Unmute microphone"))
			mic = not mic;
		ImGui::SameLine();
		ui::icon_button(ICON_FA_EYE_SLASH, {}, false, "Toggle passthrough");
		ImGui::SameLine();
		ui::icon_button(ICON_FA_HAND, {}, false, "Hand tracking");
		ImGui::SameLine();
		ui::icon_button(ICON_FA_ARROW_ROTATE_LEFT, {}, false, "Recenter");

		ui::end_card();
	}

	// Toggles
	ui::begin_card("##toggles");
	{
		card_title("TOGGLES");

		// room for the toggle plus a trailing reset slot
		const float toggle_w = ImGui::GetFrameHeight() * ui::metrics::control_height * ui::metrics::toggle_aspect;
		const float toggle_row_w = toggle_w + ImGui::GetFrameHeight() * ui::metrics::control_height + ImGui::GetStyle().ItemSpacing.x;

		static const bool spacewarp_default = false;
		static bool spacewarp = true;
		ui::setting_label("Application SpaceWarp", "Synthesises in-between frames so motion stays smooth", toggle_row_w);
		ui::toggle("##spacewarp", &spacewarp, &spacewarp_default);

		ui::row_separator();

		static const bool high_power_default = true;
		static bool high_power = true;
		ui::setting_label("High power mode", "Higher sustained clocks for steadier frames", toggle_row_w);
		ui::toggle("##high_power", &high_power, &high_power_default);

		ui::end_card();
	}

	// Sliders
	ui::begin_card("##sliders");
	{
		card_title("SLIDERS");

		static const int resolution_default = 100;
		static int resolution = 145;
		ui::setting_label("Render resolution", "Pixels rendered per eye", control_w);
		ui::slider_int("##resolution", &resolution, 50, 350, "%d%%", {control_w, 0}, &resolution_default);

		ui::row_separator();

		static const int bitrate_default = 100;
		static int bitrate = 165;
		ui::setting_label("Bitrate", "Video data rate", control_w);
		ui::slider_int("##bitrate", &bitrate, 10, 500, "%d Mbit/s", {control_w, 0}, &bitrate_default);

		ui::end_card();
	}

	// Segmented control
	ui::begin_card("##segmented");
	{
		card_title("SEGMENTED CONTROL");

		static const int refresh_default = 0;
		static int refresh = 2;
		static const std::vector<std::string> rates = {"Auto", "72", "80", "90", "120"};
		ui::setting_label("Refresh rate", "Frames per second shown in the headset", control_w);
		ui::segmented("##refresh", rates, &refresh, {control_w, 0}, &refresh_default);

		ui::end_card();
	}

	// Combo
	ui::begin_card("##combo");
	{
		card_title("COMBO");

		static const std::vector<ui::combo_item> codecs = {
		        {"Automatic", "Picks the best codec your PC supports"},
		        {"H.264", "Lowest latency, widest support"},
		        {"HEVC (H.265)", "Better quality per bit"},
		        {"AV1", "Best efficiency, newest GPUs only"},
		};
		static const int codec_default = 0;
		static int selected = 2;

		ui::setting_label("Video codec", "How video is compressed and sent to the headset", control_w);
		ui::combo("##codec", "Video codec", codecs, &selected, control_w, &codec_default);

		ui::end_card();
	}

	// Chips
	ui::begin_card("##chips");
	{
		card_title("CHIPS");

		ui::chip(ICON_FA_BOLT "  Auto", ui::chip_style::accent);
		ImGui::SameLine();
		ui::chip(ICON_FA_BATTERY_HALF "  38%", ui::chip_style::success);
		ImGui::SameLine();
		ui::chip("Connected", ui::chip_style::success, true);
		ImGui::SameLine();
		ui::chip("Not connected", ui::chip_style::muted, true);
		ImGui::SameLine();
		ui::chip("Unavailable", ui::chip_style::muted);
		ImGui::SameLine();
		ui::chip("Beta", ui::chip_style::warning);

		ui::end_card();
	}

	// Text & number inputs
	ui::begin_card("##inputs");
	{
		card_title("INPUTS");

		static std::string name, address;
		static int port = 9757;

		ui::setting_label("Name", "Friendly label for the server", control_w);
		ui::input_text("##name", name, "My gaming PC", control_w);

		ui::row_separator();

		ui::setting_label("Address", "Hostname or IP of the PC", control_w);
		ui::input_text("##address", address, "192.168.1.10 or host.local", control_w);

		ui::row_separator();

		ui::setting_label("Port", "TCP/UDP port the server listens on", control_w);
		ui::input_int("##port", &port, 1, 1, 65535, control_w);

		ui::end_card();
	}

	// Dialog
	ui::begin_card("##dialog");
	{
		card_title("DIALOG");

		if (ui::button("Add server"))
			ImGui::OpenPopup("##add_server_demo");

		static std::string d_name, d_address;
		static int d_port = 9757;
		if (ui::begin_modal("##add_server_demo", "Add server", 620))
		{
			const float w = ImGui::GetContentRegionAvail().x;
			ImGui::TextUnformatted("Name");
			ui::input_text("##d_name", d_name, "My gaming PC", w);
			ImGui::Dummy({0, 4});
			ImGui::TextUnformatted("Address");
			ui::input_text("##d_address", d_address, "192.168.1.10 or host.local", w);
			ImGui::Dummy({0, 4});
			ImGui::TextUnformatted("Port");
			ui::input_int("##d_port", &d_port, 1, 1, 65535, w);

			ImGui::Dummy({0, 12});

			// right-aligned footer
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const float cancel_w = ImGui::CalcTextSize("Cancel").x + ui::metrics::button_padding.x * 2;
			const float save_w = ImGui::CalcTextSize("Save").x + ui::metrics::button_padding.x * 2;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - cancel_w - save_w - gap);

			if (ui::button("Cancel", ui::button_style::secondary))
				ImGui::CloseCurrentPopup();
			ImGui::SameLine();
			if (ui::button("Save", ui::button_style::primary))
				ImGui::CloseCurrentPopup();

			ui::end_modal();
		}

		ui::end_card();
	}

	ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
}
