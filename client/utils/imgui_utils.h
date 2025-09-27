/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#pragma once

#include "imgui.h"
#include <string>

void ScrollWhenDragging();

void CenterTextH(const std::string & text);

void CenterTextHV(const std::string & text);

void InputText(const char * label, std::string & text, const ImVec2 & size, ImGuiInputTextFlags flags);

bool RadioButtonWithoutCheckBox(const std::string & label, bool active, ImVec2 size_arg);

template <typename T, typename U>
static bool RadioButtonWithoutCheckBox(const std::string & label, T & v, U v_button, ImVec2 size_arg)
{
	const bool pressed = RadioButtonWithoutCheckBox(label, v == v_button, size_arg);
	if (pressed)
		v = v_button;
	return pressed;
}
