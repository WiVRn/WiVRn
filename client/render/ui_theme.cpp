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

#include "ui_theme.h"

namespace wivrn::ui
{

static ImVec4 rgb(int r, int g, int b, float a = 1)
{
	return {r / 255.f, g / 255.f, b / 255.f, a};
}

static theme dark_default()
{
	theme t;
	t.name = "Dark";

	t.accent = rgb(59, 130, 246);
	t.accent_hovered = rgb(82, 146, 247);
	t.accent_active = rgb(43, 110, 222);
	t.on_accent = rgb(255, 255, 255);

	t.background = rgb(10, 11, 13);
	t.card = rgb(22, 24, 29);
	t.card_hovered = rgb(28, 30, 36);
	t.control = rgb(35, 38, 46);
	t.control_hovered = rgb(44, 48, 57);
	t.control_active = rgb(52, 57, 68);

	t.text = rgb(244, 245, 248);
	t.text_muted = rgb(150, 156, 167);
	t.border = rgb(40, 43, 51);

	t.danger = rgb(239, 68, 68);
	t.danger_hovered = rgb(248, 96, 96);
	t.success = rgb(52, 199, 123);
	t.warning = rgb(249, 115, 6);

	t.rounding = 8;
	t.card_rounding = 14;
	t.border_size = 1;
	t.font_scale = 1.0;         // user multiplier; 100% maps to metrics::font_base
	t.background_alpha = 0.88f; // ~ the previous hardcoded 224/255
	return t;
}

static theme midnight()
{
	theme t = dark_default();
	t.name = "Midnight";
	t.background = rgb(6, 8, 16);
	t.card = rgb(16, 20, 34);
	t.card_hovered = rgb(22, 27, 44);
	t.control = rgb(28, 34, 54);
	t.control_hovered = rgb(36, 43, 66);
	t.control_active = rgb(44, 52, 78);
	t.border = rgb(30, 37, 58);
	return t;
}

static theme slate_light()
{
	theme t = dark_default();
	t.name = "Slate";
	t.background = rgb(28, 30, 36);
	t.card = rgb(38, 41, 49);
	t.card_hovered = rgb(46, 50, 60);
	t.control = rgb(54, 59, 70);
	t.control_hovered = rgb(64, 70, 83);
	t.control_active = rgb(74, 81, 96);
	t.text = rgb(236, 238, 242);
	t.text_muted = rgb(168, 174, 186);
	t.border = rgb(58, 63, 75);
	return t;
}

// True black, ideal for OLED panels
static theme oled()
{
	theme t = dark_default();
	t.name = "OLED";
	t.background = rgb(0, 0, 0);
	t.card = rgb(6, 6, 8);
	t.card_hovered = rgb(11, 12, 14);
	t.control = rgb(22, 24, 28);
	t.control_hovered = rgb(30, 32, 38);
	t.control_active = rgb(40, 43, 50);
	t.border = rgb(26, 28, 33);
	return t;
}

// Neutral dark gray
static theme graphite()
{
	theme t = dark_default();
	t.name = "Graphite";
	t.background = rgb(20, 21, 23);
	t.card = rgb(31, 33, 36);
	t.card_hovered = rgb(39, 41, 45);
	t.control = rgb(48, 51, 56);
	t.control_hovered = rgb(58, 62, 68);
	t.control_active = rgb(68, 72, 80);
	t.text = rgb(238, 240, 243);
	t.text_muted = rgb(160, 164, 172);
	t.border = rgb(50, 53, 59);
	return t;
}

// Light surfaces, dark text
static theme light()
{
	theme t = dark_default();
	t.name = "Light";
	t.background = rgb(236, 238, 242);
	t.card = rgb(255, 255, 255);
	t.card_hovered = rgb(246, 247, 250);
	t.control = rgb(212, 216, 224);
	t.control_hovered = rgb(199, 204, 213);
	t.control_active = rgb(184, 190, 201);
	t.text = rgb(22, 24, 30);
	t.text_muted = rgb(108, 114, 126);
	t.border = rgb(205, 209, 217);
	return t;
}

std::vector<theme> presets()
{
	return {oled(), dark_default(), midnight(), graphite(), slate_light(), light()};
}

std::vector<accent_swatch> accent_swatches()
{
	return {
	        {"Blue", rgb(59, 130, 246), rgb(82, 146, 247), rgb(43, 110, 222)},
	        {"Violet", rgb(139, 92, 246), rgb(157, 116, 247), rgb(120, 72, 226)},
	        {"Emerald", rgb(16, 185, 129), rgb(34, 200, 145), rgb(8, 160, 110)},
	        {"Amber", rgb(245, 158, 11), rgb(248, 173, 40), rgb(220, 138, 6)},
	        {"Rose", rgb(244, 63, 94), rgb(247, 90, 117), rgb(222, 48, 78)},
	        {"Cyan", rgb(6, 182, 212), rgb(34, 197, 224), rgb(6, 158, 184)},
	};
}

void set_accent(const accent_swatch & swatch)
{
	theme & t = current();
	t.accent = swatch.base;
	t.accent_hovered = swatch.hovered;
	t.accent_active = swatch.active;
}

theme & current()
{
	static theme t = dark_default();
	return t;
}

void set_theme(const theme & t)
{
	current() = t;
}

void theme::apply() const
{
	ImGuiStyle & style = ImGui::GetStyle();

	style.FontScaleMain = font_scale;
	style.WindowRounding = card_rounding;
	style.ChildRounding = card_rounding;
	style.PopupRounding = card_rounding;
	style.FrameRounding = rounding;
	style.GrabRounding = rounding;
	style.TabRounding = rounding;
	style.FrameBorderSize = border_size;
	style.WindowBorderSize = border_size;

	ImVec4 * c = style.Colors;
	c[ImGuiCol_Text] = text;
	c[ImGuiCol_TextDisabled] = text_muted;
	c[ImGuiCol_WindowBg] = background;
	c[ImGuiCol_ChildBg] = card;
	c[ImGuiCol_PopupBg] = card;
	c[ImGuiCol_Border] = border;
	c[ImGuiCol_FrameBg] = control;
	c[ImGuiCol_FrameBgHovered] = control_hovered;
	c[ImGuiCol_FrameBgActive] = control_active;
	c[ImGuiCol_Button] = control;
	c[ImGuiCol_ButtonHovered] = control_hovered;
	c[ImGuiCol_ButtonActive] = accent;
	c[ImGuiCol_Header] = control_hovered;
	c[ImGuiCol_HeaderHovered] = control_hovered;
	c[ImGuiCol_HeaderActive] = accent;
	c[ImGuiCol_SliderGrab] = accent;
	c[ImGuiCol_SliderGrabActive] = accent_active;
	c[ImGuiCol_CheckMark] = accent;
	c[ImGuiCol_ScrollbarBg] = {0, 0, 0, 0};
	c[ImGuiCol_ScrollbarGrab] = control;
	c[ImGuiCol_ScrollbarGrabHovered] = control_hovered;
	c[ImGuiCol_ScrollbarGrabActive] = control_active;
	c[ImGuiCol_Separator] = border;
}

} // namespace wivrn::ui
