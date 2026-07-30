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

#include "lobby.h"

#include "application.h"
#include "configuration.h"
#include "render/ui_theme.h"
#include <string>

namespace ui = wivrn::ui;

void scenes::lobby::apply_theme_settings()
{
	auto & config = application::get_config();

	// preset by name, surfaces only, accent applied separately below
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
