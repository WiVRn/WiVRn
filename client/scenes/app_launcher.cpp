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

#include "constants.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "scenes/stream.h"
#include "utils/i18n.h"
#include "utils/ranges.h"

#include <imspinner.h>
#include <spdlog/fmt/fmt.h>
#include <uni_algo/case.h>

using namespace std::chrono_literals;

// Display a button with an image and a text centred horizontally
static bool icon(
        const std::string & text,
        ImTextureRef tex_ref,
        const ImVec2 & image_size,
        ImGuiButtonFlags flags = 0,
        const ImVec2 & size_arg = ImVec2(0, 0),
        const ImVec2 & uv0 = ImVec2(0, 0),
        const ImVec2 & uv1 = ImVec2(1, 1),
        const ImVec4 & tint_col = ImVec4(1, 1, 1, 1))
{
	// Based on ImGui::ButtonEx and ImGui::ImageButtonEx
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	const ImGuiStyle & style = ImGui::GetStyle();

	if (window->SkipItems)
		return false;

	const ImVec2 label_size = ImGui::CalcTextSize(text.c_str(), nullptr, true);

	ImVec2 size = ImGui::CalcItemSize(
	        size_arg,
	        std::max(image_size.x, label_size.x) + style.FramePadding.x * 2.0f,
	        image_size.y + style.ItemInnerSpacing.y + label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);

	ImRect image_pos(
	        {(bb.Min.x + bb.Max.x - image_size.x) / 2, bb.Min.y + style.FramePadding.y},
	        {(bb.Min.x + bb.Max.x + image_size.x) / 2, bb.Min.y + style.FramePadding.y + image_size.y});

	ImRect label_pos(
	        {bb.Min.x + style.FramePadding.x, image_pos.Max.y + style.ItemInnerSpacing.y},
	        {bb.Max.x - style.FramePadding.x, bb.Max.y - style.FramePadding.y});

	ImGui::ItemSize(bb);

	ImGuiID id = window->GetID(text.c_str());
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held, flags);

	// Render
	const ImU32 col = ImGui::GetColorU32((held && hovered) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered
	                                                                                         : ImGuiCol_Button);
	ImGui::RenderNavCursor(bb, id);
	ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

	window->DrawList->AddImage(tex_ref, image_pos.Min, image_pos.Max, uv0, uv1, ImGui::GetColorU32(tint_col));
	ImGui::RenderTextClipped(label_pos.Min, label_pos.Max, text.c_str(), NULL, &label_size, style.ButtonTextAlign, &bb);

	return pressed;
}

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
	try
	{
		default_icon = textures.load_texture("default_icon.ktx2");
	}
	catch (...)
	{
		default_icon = textures.load_texture("default_icon.png");
	}
}

app_launcher::~app_launcher()
{
	stream.device.waitIdle();
	for (const auto & [app_id, app_icon]: app_icons)
		textures.free_texture(app_icon);
	textures.free_texture(default_icon);
}

app_launcher::clicked app_launcher::draw_gui(imgui_context & imgui_ctx, const std::string & cancel)
{
	auto res = clicked::None;
	auto t0 = std::chrono::steady_clock::now();
	bool app_starting = start_time != std::chrono::steady_clock::time_point{} and
	                    t0 - start_time < 10s and
	                    stream.current_state() != scenes::stream::state::streaming;

	auto cancel_size = ImGui::CalcTextSize(cancel.c_str());

	auto apps = applications.lock();

	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	if (server_name.empty())
		CenterTextH(_("Connected to WiVRn server"));
	else
		CenterTextH(fmt::format(_F("Connected to {}"), server_name));
	ImGui::PopFont();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {20, 20});
	if (apps->empty())
	{
		CenterTextHV(_("Start an application on the server to start streaming."));
	}
	else
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 0});
		ImGui::BeginDisabled(app_starting);
		ImGui::BeginChild("Main", ImGui::GetWindowSize() - ImGui::GetCursorPos() - ImVec2(0, cancel_size.y + 80), 0, app_starting ? ImGuiWindowFlags_NoScrollWithMouse : 0);

		ImGui::Indent(20);
		if (server_name.empty())
			ImGui::Text("%s", _S("Start an application on your computer or select one to start streaming."));
		else
			ImGui::Text("%s", fmt::format(_F("Start an application on {} or select one to start streaming."), server_name).c_str());
		ImGui::Unindent();

		float icon_width = 400;
		float icon_spacing = ImGui::GetStyle().ItemSpacing.x;
		float usable_window_width = ImGui::GetWindowSize().x - ImGui::GetCurrentWindow()->ScrollbarSizes.x;

		int icons_per_line = (usable_window_width + icon_spacing) / (icon_width + icon_spacing);
		float icon_line_width = icons_per_line * icon_width + (icons_per_line - 1) * icon_spacing;

		ImGui::Indent((usable_window_width - icon_line_width) / 2);

		for (const auto [index, app]: utils::enumerate(*apps))
		{
			ImTextureID texture = [&]() -> ImTextureID {
				if (app.image.empty())
					return default_icon;
				else
				{
					auto it = app_icons.find(app.id);
					if (it == app_icons.end())
					{
						// Don't load too many textures at the same time to keep the GUI responsive
						if (std::chrono::steady_clock::now() - t0 > 10ms)
							return default_icon;

						try
						{
							it = app_icons.emplace(app.id, textures.load_texture(app.image)).first;
						}
						catch (std::exception & e)
						{
							spdlog::warn("Unable to load icon for \"{}\": {}", app.id, e.what());

							app.image.clear();
							return default_icon;
						}
					}
					return it->second;
				}
			}();

			ImGui::PushStyleColor(ImGuiCol_Button, 0);
			if (icon(app.name + "##" + app.id, texture, {256, 256}, ImGuiButtonFlags_PressedOnClickRelease, {icon_width, 0}))
			{
				res = clicked::Start;
				start_time = t0;
				stream.start_application(app.id);
			}
			imgui_ctx.vibrate_on_hover();
			ImGui::PopStyleColor(); // ImGuiCol_Button

			if ((index + 1) % icons_per_line)
				ImGui::SameLine();
		}
		ImGui::Unindent();

		std::vector<std::pair<std::string, ImTextureID>> to_be_removed;
		for (const auto & [app_id, app_icon]: app_icons)
		{
			if (not std::ranges::contains(*apps, app_id, &app::id))
				to_be_removed.emplace_back(app_id, app_icon);
		}
		for (const auto & [app_id, app_icon]: to_be_removed)
		{
			imgui_ctx.free_texture(app_icon);
			app_icons.erase(app_id);
		}

		ScrollWhenDragging();
		ImGui::EndChild();
		ImGui::EndDisabled();

		if (app_starting)
		{
			ImGui::SetCursorPos(ImGui::GetWindowSize() / 2 - ImVec2{200, 200} - ImGui::GetStyle().FramePadding);
			ImSpinner::SpinnerAng("App starting spinner",
			                      200,                         // Radius
			                      40,                          // Thickness
			                      ImColor{1.f, 1.f, 1.f, 1.f}, // Colour
			                      ImColor{1.f, 1.f, 1.f, 0.f}, // Background
			                      6,                           // Velocity
			                      0.75f * 2 * M_PI             // Angle
			);
		}

		ImGui::PopStyleVar(); // ImGuiStyleVar_WindowPadding
	}
	ImGui::PopStyleVar();

	ImGui::SetCursorPos(ImGui::GetWindowSize() - cancel_size - ImVec2{50, 50});

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 10});
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.00f));
	if (ImGui::Button(cancel.c_str()))
		res = clicked::Cancel;
	imgui_ctx.vibrate_on_hover();
	ImGui::PopStyleColor(3); // ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive
	ImGui::PopStyleVar(2);
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
