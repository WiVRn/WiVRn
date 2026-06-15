/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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
#include "app_launcher.h"

#include "application.h"
#include "configuration.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "render/ui_theme.h"
#include "render/ui_widgets.h"
#include "scenes/stream.h"
#include "utils/i18n.h"
#include "utils/ranges.h"

#include <IconsFontAwesome6.h>
#include <algorithm>
#include <imspinner.h>
#include <spdlog/fmt/fmt.h>
#include <uni_algo/case.h>

using namespace std::chrono_literals;

namespace ui = wivrn::ui;

namespace
{
// target grid icon size for the small/medium/large setting; tiles stretch from this to fill the row
float grid_image_size(uint32_t size)
{
	switch (size)
	{
		case 0:
			return ui::metrics::app_icon_small;
		case 2:
			return ui::metrics::app_icon_large;
		default:
			return ui::metrics::app_icon_medium;
	}
}

// Themed grid tile: image with a centred label below, control surface, accent on hover
bool app_tile(const std::string & id, const std::string & name, ImTextureID texture, float tile_w, float image_size)
{
	const ui::theme & t = ui::current();
	const ImGuiStyle & style = ImGui::GetStyle();
	ImGuiWindow * window = ImGui::GetCurrentWindow();

	if (window->SkipItems)
		return false;

	const float pad = ui::metrics::list_row_pad;
	const ImVec2 size{tile_w, image_size + pad * 2 + style.ItemInnerSpacing.y + ImGui::GetTextLineHeight()};
	const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);

	ImGui::ItemSize(bb);
	const ImGuiID iid = window->GetID(id.c_str());
	if (not ImGui::ItemAdd(bb, iid))
		return false;

	bool hovered, held;
	const bool pressed = ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_PressedOnClickRelease);

	ImDrawList * dl = window->DrawList;
	dl->AddRectFilled(bb.Min, bb.Max, t.col(held and hovered ? t.control_active : hovered ? t.card_hovered
	                                                                                      : t.control),
	                  t.card_rounding);
	if (hovered)
		dl->AddRect(bb.Min, bb.Max, t.col(t.accent), t.card_rounding, 0, t.border_size * 2);

	const ImVec2 image_min{(bb.Min.x + bb.Max.x - image_size) / 2, bb.Min.y + pad};
	dl->AddImage(texture, image_min, image_min + ImVec2{image_size, image_size});

	const ImVec2 label_min{bb.Min.x + pad, image_min.y + image_size + style.ItemInnerSpacing.y};
	const ImVec2 label_max{bb.Max.x - pad, bb.Max.y - pad};
	const ImVec2 label_size = ImGui::CalcTextSize(name.c_str(), nullptr, true);
	ImGui::RenderTextClipped(label_min, label_max, name.c_str(), nullptr, &label_size, {0.5f, 0.5f}, &bb);

	return pressed;
}
} // namespace

app_launcher::app_launcher(
        scenes::stream & stream,
        std::string server_name) :
        server_name(std::move(server_name)),
        stream(stream),
        textures(
                stream.physical_device,
                stream.device,
                stream.queue_family_index,
                stream.queue)
{
	default_icon = textures.load_texture("assets://default_icon.ktx2");
}

app_launcher::clicked app_launcher::draw_gui(imgui_context & imgui_ctx, const std::string & cancel)
{
	auto & config = application::get_config();

	auto res = clicked::None;
	auto t0 = std::chrono::steady_clock::now();
	bool app_starting = start_time != std::chrono::steady_clock::time_point{} and
	                    t0 - start_time < 10s;

	auto apps = applications.lock();

	// resolve an app's icon, uploading lazily and capped per frame to stay responsive
	auto texture_for = [&](app & a) -> ImTextureID {
		if (a.image.empty())
			return default_icon;
		auto it = app_icons.find(a.id);
		if (it == app_icons.end())
		{
			if (std::chrono::steady_clock::now() - t0 > 10ms)
				return default_icon;
			try
			{
				it = app_icons.emplace(a.id, textures.load_texture(a.image)).first;
			}
			catch (std::exception & e)
			{
				spdlog::warn("Unable to load icon for \"{}\": {}", a.id, e.what());
				a.image.clear();
				return default_icon;
			}
		}
		return it->second;
	};

	auto launch = [&](const std::string & id) {
		res = clicked::Start;
		start_time = t0;
		stream.start_application(id);
	};

	const ImVec2 content_pad = {34, 34}; // symmetric inner padding, both axes equal
	const float margin = content_pad.x;
	const float button_h = ImGui::GetFrameHeight() * ui::metrics::control_height;
	const float footer_h = button_h + margin * 2;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, content_pad);
	ImGui::BeginChild("launcher", {0, ImGui::GetContentRegionAvail().y - footer_h}, ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGui::BeginDisabled(app_starting);

	const float header_top = ImGui::GetCursorPosY();
	const std::string subtitle = _S("Pick an application to start streaming, or start one on the PC.");
	if (server_name.empty())
		ui::page_header(_S("Connected"), subtitle);
	else
		ui::page_header(fmt::format(_F("Connected to {}"), server_name), subtitle);

	if (apps->empty())
	{
		ImGui::Spacing();
		ImGui::TextUnformatted(_S("Waiting for an application on the server"));
	}
	else
	{
		const float content_y = ImGui::GetCursorPosY();

		int view = config.app_list_view ? 1 : 0;
		int size_idx = std::clamp<int>(config.app_icon_size, 0, 2);
		const std::vector<std::string> size_opts = {_S("Small"), _S("Medium"), _S("Large")};
		const std::vector<std::string> view_opts = {ICON_FA_TABLE_CELLS_LARGE, ICON_FA_LIST};
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const float view_w = ui::metrics::app_view_toggle_width;
		const float size_w = ui::metrics::app_size_toggle_width;
		const float cluster = view_w + (view == 0 ? size_w + gap : 0);

		// grid/list and icon size on the title row, right-aligned and top-aligned with the title
		ImGui::SetCursorPos({ImGui::GetContentRegionMax().x - cluster, header_top});
		if (view == 0)
		{
			if (ui::segmented("##icon_size", size_opts, &size_idx, {size_w, 0}))
			{
				config.app_icon_size = size_idx;
				config.save();
			}
			ImGui::SameLine(0, gap);
		}
		if (ui::segmented("##view_mode", view_opts, &view, {view_w, 0}))
		{
			config.app_list_view = view == 1;
			config.save();
		}
		ImGui::SetCursorPos({ImGui::GetCursorStartPos().x, content_y});

		if (config.app_list_view)
		{
			ui::begin_list_card("##apps");
			for (auto & app: *apps)
			{
				if (ui::begin_list_row(app.id.c_str(), nullptr, texture_for(app), app.name, "").clicked)
					launch(app.id);
				ui::end_list_row();
			}
			ui::end_card();
		}
		else
		{
			// justified grid: as many columns as fit at the chosen icon size, then stretch
			// every tile to divide the row exactly so there is no trailing gap
			const float tile_margin = ui::metrics::app_tile_margin;
			const float target = grid_image_size(config.app_icon_size) + tile_margin * 2;
			const float avail = ImGui::GetContentRegionAvail().x;
			const int per_line = std::max(1, int((avail + gap) / (target + gap)));
			const float tile_w = int((avail - gap * (per_line - 1)) / per_line);
			const float image_size = tile_w - tile_margin * 2;
			for (const auto [index, app]: utils::enumerate(*apps))
			{
				if (app_tile(app.id, app.name, texture_for(app), tile_w, image_size))
					launch(app.id);
				imgui_ctx.vibrate_on_hover();
				if ((index + 1) % per_line)
					ImGui::SameLine();
			}
		}

		// drop textures for apps no longer listed
		std::vector<std::string> stale;
		for (const auto & [app_id, app_icon]: app_icons)
			if (not std::ranges::contains(*apps, app_id, &app::id))
				stale.push_back(app_id);
		for (const auto & app_id: stale)
		{
			imgui_ctx.free_texture(app_icons.at(app_id));
			app_icons.erase(app_id);
		}
	}

	ScrollWhenDragging();
	ImGui::EndDisabled();
	ImGui::EndChild();
	ImGui::PopStyleVar();

	if (app_starting)
	{
		const float r = ui::metrics::app_spinner_radius;
		ImGui::SetCursorPos(ImGui::GetWindowSize() / 2 - ImVec2{r, r});
		ImSpinner::SpinnerAng("starting", r, ui::metrics::app_spinner_thickness, ImColor{ui::current().accent}, ImColor{ui::current().card}, 6, 0.75f * 2 * M_PI);
	}

	// footer: disconnect / cancel, bottom-right
	ImGui::SetCursorPos({ImGui::GetWindowSize().x - ui::button_width(cancel) - margin, ImGui::GetWindowSize().y - button_h - margin});
	if (ui::button(cancel, ui::button_style::danger))
		res = clicked::Cancel;

	return res;
}

void app_launcher::operator()(to_headset::application_list && apps)
{
	std::ranges::sort(apps.applications, [](auto & l, auto & r) {
		return una::casesens::collate_utf8(l.name, r.name) < 0;
	});

	auto locked = applications.lock();

	locked->clear();
	locked->reserve(apps.applications.size());
	for (const auto & i: apps.applications)
	{
		locked->push_back(app{
		        .id = std::move(i.id),
		        .name = std::move(i.name),
		});
	}
}

void app_launcher::operator()(to_headset::application_icon && icon)
{
	auto locked = applications.lock();
	if (auto it = std::ranges::find(*locked, icon.id, &app::id); it != locked->end())
		it->image = std::move(icon.image);
}
