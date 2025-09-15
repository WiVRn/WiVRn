/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#ifdef __ANDROID__
#include "android/battery.h"
#endif
#include "application.h"
#include "asset.h"
#include "configuration.h"
#include "constants.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "lobby.h"
#include "stream.h"
#include "utils/i18n.h"
#include "utils/overloaded.h"
#include "utils/ranges.h"
#include "version.h"
#include "xr/body_tracker.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>
#include <imspinner.h>
#include <ranges>
#include <spdlog/fmt/fmt.h>
#include <string>
#include <utility>
#include <utils/strings.h>

#include "IconsFontAwesome6.h"

using namespace std::chrono_literals;

// https://github.com/ocornut/imgui/issues/3379#issuecomment-2943903877
static void ScrollWhenDragging()
{
	ImVec2 delta{0.0f, -ImGui::GetIO().MouseDelta.y};
	const ImGuiMouseButton mouse_button = ImGuiMouseButton_Left;

	ImGuiContext & g = *ImGui::GetCurrentContext();
	ImGuiWindow * window = g.CurrentWindow;
	ImGuiID id = window->GetID("##scrolldraggingoverlay");
	ImGui::KeepAliveID(id);

	static int active_id;
	static ImVec2 cumulated_delta;

	bool HoveredIdAllowOverlap_backup = std::exchange(g.HoveredIdAllowOverlap, true);
	bool ActiveIdAllowOverlap_backup = std::exchange(g.ActiveIdAllowOverlap, true);
	if (active_id == 0 and ImGui::ItemHoverable(window->Rect(), 0, g.CurrentItemFlags) and ImGui::IsMouseClicked(mouse_button, ImGuiInputFlags_None, /*id*/ ImGuiKeyOwner_Any))
	{
		active_id = id;

		// Don't scroll on the first step, in case the active controller just changed
		delta = {};
		cumulated_delta = {};
	}

	if (not g.IO.MouseDown[mouse_button])
		active_id = 0;

	if (active_id == id)
	{
		if (delta.x != 0.0f)
			ImGui::SetScrollX(window, window->Scroll.x + delta.x);
		if (delta.y != 0.0f)
			ImGui::SetScrollY(window, window->Scroll.y + delta.y);

		cumulated_delta += delta;
		if (std::max(std::abs(cumulated_delta.x), std::abs(cumulated_delta.y)) > 50)
			ImGui::ClearActiveID();
	}

	g.HoveredIdAllowOverlap = HoveredIdAllowOverlap_backup;
	g.ActiveIdAllowOverlap = ActiveIdAllowOverlap_backup;
}

static void CenterTextH(const std::string & text)
{
	float win_width = ImGui::GetWindowSize().x;
	float text_width = ImGui::CalcTextSize(text.c_str()).x;
	ImGui::SetCursorPosX((win_width - text_width) / 2);

	ImGui::Text("%s", text.c_str());
}

static void CenterTextHV(const std::string & text)
{
	ImVec2 size = ImGui::GetWindowSize();

	std::vector<std::string> lines = utils::split(text);

	float text_height = 0;
	for (const auto & i: lines)
		text_height += ImGui::CalcTextSize(i.c_str()).y;

	ImGui::SetCursorPosY((size.y - text_height) / 2);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
	for (const auto & i: lines)
	{
		float text_width = ImGui::CalcTextSize(i.c_str()).x;
		ImGui::SetCursorPosX((size.x - text_width) / 2);
		ImGui::Text("%s", i.c_str());
	}
	ImGui::PopStyleVar();
}

static void InputText(const char * label, std::string & text, const ImVec2 & size, ImGuiInputTextFlags flags)
{
	auto callback = [](ImGuiInputTextCallbackData * data) -> int {
		std::string & text = *reinterpret_cast<std::string *>(data->UserData);

		if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
		{
			assert(text.data() == data->Buf);
			text.resize(data->BufTextLen);
			data->Buf = text.data();
		}

		return 0;
	};

	ImGui::InputTextEx(label, nullptr, text.data(), text.size(), size, flags | ImGuiInputTextFlags_CallbackResize, callback, &text);
}

static void display_recentering_tip(imgui_context & ctx, const std::string & tip)
{
	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, constants::style::window_padding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2);
	ImGui::SetNextWindowPos(ctx.layers()[3].vp_center(), ImGuiCond_Always, {0.5, 0.5});
	ImGui::Begin("Recentering tip", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

	std::vector<std::string> lines = utils::split(tip);
	std::vector<float> widths;
	float max_width = 0;

	for (auto & line: lines)
	{
		widths.push_back(ImGui::CalcTextSize(line.c_str()).x);
		max_width = std::max(max_width, widths.back());
	}

	for (auto [width, line]: std::views::zip(widths, lines))
	{
		ImGui::Dummy({(max_width - width) / 2, 0});
		ImGui::SameLine();
		ImGui::TextUnformatted(line.data(), line.data() + line.size());
	}

	ImGui::End();
	ImGui::PopStyleVar(2);
	ImGui::PopFont();
}

// Display a button with an image and a text centred horizontally
static bool icon(const std::string & text, ImTextureRef tex_ref, const ImVec2 & image_size, ImGuiButtonFlags flags = 0, const ImVec2 & size_arg = ImVec2(0, 0), const ImVec2 & uv0 = ImVec2(0, 0), const ImVec2 & uv1 = ImVec2(1, 1), const ImVec4 & tint_col = ImVec4(1, 1, 1, 1))
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

std::string openxr_post_processing_flag_name(XrCompositionLayerSettingsFlagsFB flag)
{
	switch (flag)
	{
		case XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SUPER_SAMPLING_BIT_FB:
		case XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB:
			return _cS("openxr_post_processing", "Normal");
		case XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB:
		case XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB:
			return _cS("openxr_post_processing", "Quality");
		default:
			return _cS("openxr_post_processing", "Disabled");
	}
}

void scenes::lobby::gui_connecting(locked_notifiable<pin_request_data> & pin_request)
{
	using constants::style::button_size;

	std::string close_button_label = _("Disconnect");

	std::string status;
	if (next_scene)
	{
		current_tab = tab::connected;
		timestamp_start_application.reset();
		ImGui::CloseCurrentPopup();
		return;
	}
	else if (async_session.valid())
		status = async_session.get_progress();
	else if (async_error)
		status = *async_error;
	else
	{
		ImGui::CloseCurrentPopup();
		return;
	}

	if (async_error)
		close_button_label = _("Close");

	ImGui::Dummy({1000, 1});

	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	if (server_name == "")
		CenterTextH(fmt::format(_F("Connection")));
	else
		CenterTextH(fmt::format(_F("Connection to {}"), server_name));
	ImGui::PopFont();

	// ImGui::TextWrapped("%s", status.first.c_str());
	ImGui::Text("%s", status.c_str());

	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - button_size.x - ImGui::GetStyle().WindowPadding.x);

	if (ImGui::Button(close_button_label.c_str(), button_size))
	{
		async_session.cancel();
		next_scene.reset();

		pin_request->pin_cancelled = true;

		ImGui::CloseCurrentPopup();
	}
	imgui_ctx->vibrate_on_hover();
}

void scenes::lobby::gui_enter_pin(locked_notifiable<pin_request_data> & pin_request)
{
	const int pin_size = 6;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, constants::style::pin_entry_item_spacing);
	ImGui::PushFont(nullptr, constants::gui::font_size_large);

	const auto & style = ImGui::GetStyle();
	const auto window = ImGui::GetCurrentWindow();

	const std::string displayed_text = pin_buffer == "" ? _("PIN") : pin_buffer;

	const ImGuiID id = window->GetID("PIN");
	const ImVec2 label_size = ImGui::CalcTextSize(displayed_text.c_str(), nullptr, true);
	const ImVec2 size = {constants::style::pin_entry_popup_width, label_size.y + style.FramePadding.y * 2.0f};

	const ImRect bb(window->DC.CursorPos, window->DC.CursorPos + size);

	ImGui::ItemSize(size, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return;

	const ImU32 col = ImGui::GetColorU32(ImGuiCol_FrameBg);
	ImGui::RenderFrame(bb.Min, bb.Max, col, true, style.FrameRounding);

	ImGui::BeginDisabled(pin_buffer == "");
	const ImVec2 alignment = pin_buffer == "" ? ImVec2{0.5, 0.5} : ImVec2{0, 0.5};
	ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, displayed_text.c_str(), nullptr, &label_size, alignment, &bb);
	ImGui::EndDisabled();

	ImGui::PopFont();
	if (ImGui::IsItemHovered())
		imgui_ctx->tooltip(_("Input the PIN displayed on the dashboard"));

	ImGui::BeginDisabled(pin_buffer.size() == pin_size);
	for (int i = 1; i <= 9;)
	{
		for (int j = 0; j < 3; j++, i++)
		{
			char button_text[] = {char('0' + i), 0};
			if (ImGui::Button(button_text, constants::style::pin_entry_key_size))
				pin_buffer += button_text;
			imgui_ctx->vibrate_on_hover();

			if (j < 2)
				ImGui::SameLine();
		}
	}

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.00f));
	if (ImGui::Button(ICON_FA_RECTANGLE_XMARK, constants::style::pin_entry_key_size))
	{
		async_session.cancel();
		next_scene.reset();
		pin_request->pin_cancelled = true;
		pin_request.notify_one();

		ImGui::CloseCurrentPopup();
	}
	imgui_ctx->vibrate_on_hover();
	ImGui::PopStyleColor(3);

	ImGui::SameLine();
	if (ImGui::Button("0", constants::style::pin_entry_key_size))
		pin_buffer += "0";
	imgui_ctx->vibrate_on_hover();
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(pin_buffer.size() == 0);
	if (ImGui::Button(ICON_FA_DELETE_LEFT, constants::style::pin_entry_key_size))
		pin_buffer.resize(pin_buffer.size() - 1);
	imgui_ctx->vibrate_on_hover();
	ImGui::EndDisabled();

	ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing

	if (pin_buffer.size() == pin_size)
	{
		pin_request->pin = pin_buffer;
		pin_request->pin_requested = false;
		pin_request.notify_one();
	}
	imgui_ctx->vibrate_on_hover();
}

void scenes::lobby::gui_connected(XrTime predicted_display_time)
{
	if (not next_scene)
	{
		current_tab = tab::server_list;

		for (const auto & [app_id, app_icon]: app_icons)
			imgui_ctx->free_texture(app_icon);
		app_icons.clear();

		return;
	}

	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	if (server_name.empty())
		CenterTextH(_("Connected to WiVRn server"));
	else
		CenterTextH(fmt::format(_F("Connected to {}"), server_name));
	ImGui::PopFont();

	auto disconnect = _("Disconnect");
	auto disconnect_size = ImGui::CalcTextSize(disconnect.c_str());

	auto apps = next_scene->get_applications();
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {20, 20});
	if (apps->empty())
	{
		CenterTextHV(_("Start an application on the server to start streaming."));
	}
	else
	{
		bool app_starting = timestamp_start_application and (predicted_display_time - *timestamp_start_application < 10'000'000'000);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 0});
		ImGui::BeginDisabled(app_starting);
		ImGui::BeginChild("Main", ImGui::GetWindowSize() - ImGui::GetCursorPos() - ImVec2(0, disconnect_size.y + 80), 0, app_starting ? ImGuiWindowFlags_NoScrollWithMouse : 0);

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

		auto t0 = std::chrono::steady_clock::now();
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
							it = app_icons.emplace(app.id, imgui_ctx->load_texture(app.image)).first;
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
				timestamp_start_application = predicted_display_time;
				next_scene->start_application(app.id);
			}
			imgui_ctx->vibrate_on_hover();
			ImGui::PopStyleColor(); // ImGuiCol_Button

			if (index % 3 != 2)
				ImGui::SameLine();
		}
		ImGui::Unindent();

		std::vector<std::pair<std::string, ImTextureID>> to_be_removed;
		for (const auto & [app_id, app_icon]: app_icons)
		{
			if (not std::ranges::contains(*apps, app_id, &scenes::stream::app::id))
				to_be_removed.emplace_back(app_id, app_icon);
		}
		for (const auto & [app_id, app_icon]: to_be_removed)
		{
			imgui_ctx->free_texture(app_icon);
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

	ImGui::SetCursorPos(ImGui::GetWindowSize() - disconnect_size - ImVec2{50, 50});

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.00f));
	if (ImGui::Button(disconnect.c_str()))
	{
		next_scene.reset();
		current_tab = tab::server_list;
	}
	imgui_ctx->vibrate_on_hover();
	ImGui::PopStyleColor(3); // ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive
}

void scenes::lobby::gui_new_server()
{
	using constants::style::button_size;

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {20, 20});
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10, 10});
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 10});
	ImGui::Dummy({1000, 1});

	ImGui::BeginTable("table", 2);

	ImGui::TableSetupColumn("Field name", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Field value", ImGuiTableColumnFlags_WidthStretch);

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	// Make sure the label is vertically centered wrt the text input
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
	ImGui::Text("%s", _S("Name"));

	ImGui::TableNextColumn();
	if (ImGui::IsWindowAppearing())
		ImGui::SetKeyboardFocusHere();
	InputText("##Name", add_server_window_prettyname, {ImGui::GetContentRegionAvail().x, 0}, 0);
	imgui_ctx->vibrate_on_hover();

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
	ImGui::Text("%s", _S("Address"));

	ImGui::TableNextColumn();
	InputText("##Hostname", add_server_window_hostname, {ImGui::GetContentRegionAvail().x, 0}, 0);
	imgui_ctx->vibrate_on_hover();

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
	ImGui::Text("%s", _S("Port"));

	ImGui::TableNextColumn();
	ImGui::InputInt("##Port", &add_server_window_port, 1, 1, ImGuiInputTextFlags_CharsDecimal);
	imgui_ctx->vibrate_on_hover();

	ImGui::EndTable();

	ImGui::Checkbox(_S("TCP only"), &add_server_tcp_only);
	imgui_ctx->vibrate_on_hover();

	auto top_left = ImGui::GetWindowContentRegionMin();
	auto bottom_right = ImGui::GetWindowContentRegionMax();

	ImGui::SetCursorPosX(top_left.x);
	if (ImGui::Button(_S("Cancel"), button_size))
	{
		current_tab = tab::server_list;
		add_server_window_prettyname = "";
		add_server_window_hostname = "";
		add_server_window_port = wivrn::default_port;
		add_server_tcp_only = false;
		add_server_cookie = "";
		ImGui::CloseCurrentPopup();
	}
	imgui_ctx->vibrate_on_hover();

	ImGui::SameLine(bottom_right.x - button_size.x);

	if (ImGui::Button(_S("Save"), button_size))
	{
		current_tab = tab::server_list;
		configuration::server_data data{
		        .manual = true,
		        .service = {
		                .name = add_server_window_prettyname,
		                .hostname = add_server_window_hostname,
		                .port = add_server_window_port,
		                .tcp_only = add_server_tcp_only,
		        },
		};

		auto & config = application::get_config();
		if (add_server_cookie != "")
			config.servers.erase(add_server_cookie);

		config.servers.emplace("manual-" + data.service.name, data);
		config.save();

		add_server_window_prettyname = "";
		add_server_window_hostname = "";
		add_server_window_port = wivrn::default_port;
		add_server_tcp_only = false;
		add_server_cookie = "";
		ImGui::CloseCurrentPopup();
	}
	imgui_ctx->vibrate_on_hover();

	ImGui::PopStyleVar(4); // ImGuiStyleVar_FrameRounding, ImGuiStyleVar_ItemSpacing, ImGuiStyleVar_CellPadding, ImGuiStyleVar_FramePadding
}

void scenes::lobby::gui_server_list()
{
	using constants::style::button_size;
	using constants::style::icon_button_size;

	auto & config = application::get_config();
	// Build an index of the cookies sorted by server name
	std::multimap<std::string, std::string> sorted_cookies;
	for (auto && [cookie, data]: config.servers)
	{
		sorted_cookies.emplace(data.service.name, cookie);
	}

	const float list_item_height = 100;
	auto & style = ImGui::GetStyle();

	std::string cookie_to_remove;
	if (sorted_cookies.empty())
	{
		ImGui::PushFont(nullptr, constants::gui::font_size_large);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.5));
		CenterTextHV(_("Start a WiVRn server on your\nlocal network"));
		ImGui::PopStyleColor();
		ImGui::PopFont();
	}

	auto pos = ImGui::GetCursorPos();

	ImGui::PushStyleColor(ImGuiCol_Header, 0);
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
	for (const auto & [name, cookie]: sorted_cookies)
	{
		configuration::server_data & data = config.servers.at(cookie);
		// bool is_selected = (cookie == selected_item);

		ImGui::SetCursorPos(pos);

		// ImGui::SetNextItemAllowOverlap();

		// TODO custom widget
		// if (ImGui::Selectable(("##" + cookie).c_str(), is_selected, ImGuiSelectableFlags_None, ImVec2(0, list_item_height)))
		// 	selected_item = cookie;

		if (data.manual)
			ImGui::SetCursorPos(ImVec2(pos.x, pos.y + 25)); // FIXME compute the position correctly
		else
			ImGui::SetCursorPos(ImVec2(pos.x, pos.y));
		ImGui::Text("%s", name.c_str());

		if (!data.manual)
		{
			ImGui::SetCursorPos(ImVec2(pos.x, pos.y + 50));
			std::string label = _("Autoconnect") + "##" + cookie;
			if (ImGui::Checkbox(label.c_str(), &data.autoconnect))
				config.save();
			imgui_ctx->vibrate_on_hover();
		}

		ImVec2 button_position(ImGui::GetWindowContentRegionMax().x, pos.y + (list_item_height - button_size.y) / 2);

		button_position.x -= button_size.x + style.WindowPadding.x;
		ImGui::SetCursorPos(button_position);

		bool enable_connect_button = (data.visible and data.compatible) or data.manual;
		ImGui::BeginDisabled(!enable_connect_button);
		if (enable_connect_button)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.3f, 0.40f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.3f, 1.00f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 1.0f, 0.2f, 1.00f));
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.3f, 0.40f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.4f, 0.3f, 1.00f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.1f, 0.5f, 0.2f, 1.00f));
		}

		if (ImGui::Button((_("Connect") + "##" + cookie).c_str(), button_size))
		{
			connect(data);
			ImGui::OpenPopup("connecting");
		}
		imgui_ctx->vibrate_on_hover();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			if (!data.compatible && !data.manual)
				imgui_ctx->tooltip(_("Incompatible server version"));
			else if (!data.visible && !data.manual)
				imgui_ctx->tooltip(_("Server not available"));
		}

		ImGui::PopStyleColor(3);
		ImGui::EndDisabled();

		if (data.manual)
		{
			button_position.x -= icon_button_size.x + style.WindowPadding.x + 10;
			ImGui::SetCursorPos(button_position);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.00f));

			if (ImGui::Button((ICON_FA_TRASH_CAN "##remove-" + cookie).c_str(), icon_button_size))
				cookie_to_remove = cookie;
			imgui_ctx->vibrate_on_hover();
			ImGui::PopStyleColor(3);

			button_position.x -= icon_button_size.x + style.WindowPadding.x + 10;
			ImGui::SetCursorPos(button_position);
			if (ImGui::Button((ICON_FA_PENCIL "##edit-" + cookie).c_str(), icon_button_size))
			{
				add_server_cookie = cookie;
				add_server_window_prettyname = data.service.name;
				add_server_window_hostname = data.service.hostname;
				add_server_window_port = data.service.port;
				add_server_tcp_only = data.service.tcp_only;
				ImGui::OpenPopup("add or edit server");
			}
			imgui_ctx->vibrate_on_hover();
		}

		pos.y += 120;
	}
	ImGui::PopStyleColor(3);

	if (cookie_to_remove != "")
	{
		config.servers.erase(cookie_to_remove);
		config.save();
	}

	// Check if an automatic connection has started
	if ((async_session.valid() || next_scene) and not ImGui::IsPopupOpen("connecting"))
		ImGui::OpenPopup("connecting");

	const auto & popup_layer = imgui_ctx->layers()[1];
	const glm::vec2 popup_layer_center = popup_layer.vp_origin + popup_layer.vp_size / 2;
	ImGui::SetNextWindowPos({popup_layer_center.x, popup_layer_center.y}, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, constants::style::window_padding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, constants::style::window_rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, constants::style::window_border_size);
	if (ImGui::BeginPopupModal("connecting", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		auto pin_request = this->pin_request.lock();
		if (pin_request->pin_requested)
			gui_enter_pin(pin_request);
		else
			gui_connecting(pin_request);
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowSize({800, 0});
	ImGui::SetNextWindowPos({popup_layer_center.x, popup_layer_center.y}, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("add or edit server", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		gui_new_server();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(3);
}

void scenes::lobby::gui_settings()
{
	auto & config = application::get_config();
	ImGuiStyle & style = ImGui::GetStyle();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 20));

	if (instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
	{
		const auto & refresh_rates = session.get_refresh_rates();
		if (not refresh_rates.empty())
		{
			float active_rate = config.preferred_refresh_rate.value_or(session.get_current_refresh_rate());
			if (ImGui::BeginCombo(_S("Refresh rate"), active_rate ? fmt::format("{}", active_rate).c_str() : _cS("automatic refresh rate", "Automatic")))
			{
				if (ImGui::Selectable(_cS("automatic refresh rate", "Automatic"), config.preferred_refresh_rate == 0, ImGuiSelectableFlags_SelectOnRelease))
				{
					session.set_refresh_rate(0);
					config.preferred_refresh_rate = 0;
					config.save();
				}
				if (ImGui::IsItemHovered())
					imgui_ctx->tooltip(_("Select refresh rate based on measured application performance.\nMay cause flicker when a change happens."));
				for (float rate: refresh_rates)
				{
					if (ImGui::Selectable(fmt::format("{}", rate).c_str(), rate == config.preferred_refresh_rate, ImGuiSelectableFlags_SelectOnRelease))
					{
						session.set_refresh_rate(rate);
						config.preferred_refresh_rate = rate;
						config.save();
					}
				}
				ImGui::EndCombo();
			}
			imgui_ctx->vibrate_on_hover();

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
				imgui_ctx->vibrate_on_hover();
			}
		}
	}

	{
		const auto current = config.resolution_scale;
		const auto width = stream_view.recommendedImageRectWidth;
		const auto height = stream_view.recommendedImageRectHeight;
		auto intScale = int(current * 100);
		const auto slider = ImGui::SliderInt(
		        _("Resolution scale").append("##resolution_scale").c_str(),
		        &intScale,
		        50,
		        350,
		        fmt::format(_F("%d%% - {}x{} per eye"), int(width * current), int(height * current)).c_str());
		if (slider)
		{
			config.resolution_scale = intScale * 0.01;
			config.save();
		}
		imgui_ctx->vibrate_on_hover();
		if (width * config.resolution_scale > stream_view.maxImageRectWidth or height * config.resolution_scale > stream_view.maxImageRectHeight)
		{
			ImGui::TextColored(ImColor(0xf9, 0x73, 0x06) /*orange*/, ICON_FA_TRIANGLE_EXCLAMATION);
			ImGui::SameLine();
			ImGui::Text("%s", fmt::format(_F("Resolution larger than {}x{} may not be supported by the headset"), stream_view.maxImageRectWidth, stream_view.maxImageRectHeight).c_str());
		}
	}

	{
		bool enabled = config.check_feature(feature::microphone);
		if (ImGui::Checkbox(_S("Enable microphone"), &enabled))
		{
			config.set_feature(feature::microphone, enabled);
		}
		imgui_ctx->vibrate_on_hover();
	}
	{
		ImGui::BeginDisabled(not config.check_feature(feature::microphone));
		ImGui::Indent();
		if (ImGui::Checkbox(_S("Unprocessed Microphone Audio"), &config.mic_unprocessed_audio))
		{
			config.save();
		}
		imgui_ctx->vibrate_on_hover();
		if (ImGui::IsItemHovered())
			imgui_ctx->tooltip(_("Force disable audio filters, such as noise cancellation"));
		ImGui::Unindent();
		ImGui::EndDisabled();
	}
	{
		ImGui::BeginDisabled(not system.hand_tracking_supported());
		bool enabled = config.check_feature(feature::hand_tracking);
		if (ImGui::Checkbox(_S("Enable hand tracking"), &enabled))
		{
			config.set_feature(feature::hand_tracking, enabled);
		}
		imgui_ctx->vibrate_on_hover();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) and (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled))
			imgui_ctx->tooltip(_("This feature is not supported by your headset"));
		ImGui::EndDisabled();
	}
	{
		ImGui::BeginDisabled(not application::get_eye_gaze_supported());
		bool enabled = config.check_feature(feature::eye_gaze);
		if (ImGui::Checkbox(_S("Enable eye tracking"), &enabled))
		{
			config.set_feature(feature::eye_gaze, enabled);
		}
		imgui_ctx->vibrate_on_hover();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) and (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled))
			imgui_ctx->tooltip(_("This feature is not supported by your headset"));
		ImGui::EndDisabled();
	}
	{
		ImGui::BeginDisabled(std::holds_alternative<std::monostate>(face_tracker));
		bool enabled = config.check_feature(feature::face_tracking);
		if (ImGui::Checkbox(_S("Enable face tracking"), &enabled))
		{
			config.set_feature(feature::face_tracking, enabled);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) and (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled))
			imgui_ctx->tooltip(_("This feature is not supported by your headset"));
		imgui_ctx->vibrate_on_hover();
	}

	auto body_tracker = system.body_tracker_supported();
	{
		ImGui::BeginDisabled(body_tracker == xr::body_tracker_type::none);
		bool enabled = config.check_feature(feature::body_tracking);
		if (ImGui::Checkbox(_S("Enable body tracking"), &enabled))
		{
			config.set_feature(feature::body_tracking, enabled);
		}
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			if (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled)
			{
				imgui_ctx->tooltip(_("This feature is not supported by your headset"));
			}
			else
			{
				if (body_tracker == xr::body_tracker_type::fb)
					imgui_ctx->tooltip(_("Requires 'Hand and body tracking' to be enabled in the Quest movement tracking settings,\notherwise body data will be guessed from controller and headset positions"));
			}
		}

		imgui_ctx->vibrate_on_hover();
	}
	if (body_tracker == xr::body_tracker_type::fb)
	{
		ImGui::BeginDisabled(not config.check_feature(feature::body_tracking));
		ImGui::Indent();
		if (ImGui::Checkbox(_S("Enable lower body tracking"), &config.fb_lower_body))
		{
			config.save();
		}
		imgui_ctx->vibrate_on_hover();
		if (ImGui::IsItemHovered())
			imgui_ctx->tooltip(_("Estimate lower body joint positions using Generative Legs\nRequires 'Hand and body tracking' to be enabled in the Quest movement tracking settings"));

		ImGui::BeginDisabled(not config.fb_lower_body);
		if (ImGui::Checkbox(_S("Enable hip tracking"), &config.fb_hip))
		{
			config.save();
		}
		imgui_ctx->vibrate_on_hover();
		if (ImGui::IsItemHovered())
			imgui_ctx->tooltip(_("Only takes affect with lower body tracking enabled\nMay be desired when using another source of hip tracking"));
		ImGui::EndDisabled();

		ImGui::Unindent();
		ImGui::EndDisabled();
	}

	ImGui::BeginDisabled(system.passthrough_supported() == xr::passthrough_type::none);
	if (ImGui::Checkbox(_S("Enable video passthrough in lobby"), &config.passthrough_enabled))
	{
		setup_passthrough();
		config.save();
	}
	imgui_ctx->vibrate_on_hover();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) and (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled))
		imgui_ctx->tooltip(_("This feature is not supported by your headset"));
	ImGui::EndDisabled();

	{
		ImGui::Checkbox(_S("Enable in-stream window"), &config.enable_stream_gui);
		imgui_ctx->vibrate_on_hover();
		if (ImGui::IsItemHovered())
			imgui_ctx->tooltip(_("Enables the configuration window to be shown while the game is streaming.\nIf enabled, the window is activated by pressing both thumbsticks."));
	}

	ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
}

void scenes::lobby::gui_post_processing()
{
	auto & config = application::get_config();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 20));

	if (application::get_openxr_post_processing_supported())
	{
		ImGui::Text("%s", _S("OpenXR post-processing"));
		ImGui::Indent();
		{
			XrCompositionLayerSettingsFlagsFB current = config.openxr_post_processing.super_sampling;
			if (ImGui::BeginCombo(_S("Supersampling"), openxr_post_processing_flag_name(current).c_str()))
			{
				const XrCompositionLayerSettingsFlagsFB selectable_options[]{
				        0,
				        XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SUPER_SAMPLING_BIT_FB,
				        XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB};
				for (XrCompositionLayerSettingsFlagsFB option: selectable_options)
				{
					if (ImGui::Selectable(openxr_post_processing_flag_name(option).c_str(), current == option, ImGuiSelectableFlags_SelectOnRelease))
					{
						config.openxr_post_processing.super_sampling = option;
						config.save();
					}
					imgui_ctx->vibrate_on_hover();
				}
				ImGui::EndCombo();
			}
			imgui_ctx->vibrate_on_hover();
			if (ImGui::IsItemHovered())
			{
				imgui_ctx->tooltip(_("Reduce flicker for high contrast edges.\nUseful when the input resolution is high compared to the headset display"));
			}
		}
		{
			XrCompositionLayerSettingsFlagsFB current = config.openxr_post_processing.sharpening;
			if (ImGui::BeginCombo(_S("Sharpening"), openxr_post_processing_flag_name(current).c_str()))
			{
				const XrCompositionLayerSettingsFlagsFB selectable_options[]{
				        0,
				        XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB,
				        XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB};
				for (XrCompositionLayerSettingsFlagsFB option: selectable_options)
				{
					if (ImGui::Selectable(openxr_post_processing_flag_name(option).c_str(), current == option, ImGuiSelectableFlags_SelectOnRelease))
					{
						config.openxr_post_processing.sharpening = option;
						config.save();
					}
					imgui_ctx->vibrate_on_hover();
				}
				ImGui::EndCombo();
			}
			imgui_ctx->vibrate_on_hover();
			if (ImGui::IsItemHovered())
			{
				imgui_ctx->tooltip(_("Improve clarity of high contrast edges and counteract blur.\nUseful when the input resolution is low compared to the headset display"));
			}
		}
		ImGui::Unindent();
	}

	ImGui::PopStyleVar();
}

#if WIVRN_CLIENT_DEBUG_MENU
void scenes::lobby::gui_debug()
{
	ImGui::GetIO().ConfigDragClickToInputText = true;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 20));

	ImGui::Checkbox("Display debug axes", &display_debug_axes);
	imgui_ctx->vibrate_on_hover();

	if (display_debug_axes)
	{
		ImGui::Checkbox("Display grip instead of aim", &display_grip_instead_of_aim);
		imgui_ctx->vibrate_on_hover();
	}

	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("##offset x", &offset_position.x, 0.0001);
	imgui_ctx->vibrate_on_hover();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("##offset y", &offset_position.y, 0.0001);
	imgui_ctx->vibrate_on_hover();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("Position", &offset_position.z, 0.0001);
	imgui_ctx->vibrate_on_hover();

	ImGui::SameLine();
	if (ImGui::Button("Reset##position"))
		offset_position = {0, 0, 0};
	imgui_ctx->vibrate_on_hover();

	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("##offset roll", &offset_orientation.x, 0.01);
	imgui_ctx->vibrate_on_hover();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("##offset pitch", &offset_orientation.y, 0.01);
	imgui_ctx->vibrate_on_hover();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("Rotation", &offset_orientation.z, 0.01);
	imgui_ctx->vibrate_on_hover();

	ImGui::SameLine();
	if (ImGui::Button("Reset##orientation"))
		offset_orientation = {0, 0, 0};
	imgui_ctx->vibrate_on_hover();

	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("Ray offset", &ray_offset, 0.0001);
	imgui_ctx->vibrate_on_hover();

	if (ImGui::Button("Delete configuration file"))
		std::filesystem::remove(application::get_config_path() / "client.json");
	imgui_ctx->vibrate_on_hover();

	float win_width = ImGui::GetWindowSize().x;
	float win_height = ImGui::GetWindowSize().y;

	ImGuiStyle & style = ImGui::GetStyle();
	ImVec2 plot_size{
	        win_width / 2 - style.ItemSpacing.x / 2,
	        win_height / 2};

	static std::array<float, 300> cpu_time;
	static std::array<float, 300> gpu_time;
	static int offset = 0;

	float min_v = 0;
	float max_v = 20;

	cpu_time[offset] = application::get_cpu_time().count() * 1.0e-6;
	gpu_time[offset] = renderer->get_gpu_time() * 1'000;
	offset = (offset + 1) % cpu_time.size();

	ImPlot::PushStyleColor(ImPlotCol_PlotBg, IM_COL32(32, 32, 32, 64));
	ImPlot::PushStyleColor(ImPlotCol_FrameBg, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBg, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBgActive, IM_COL32(0, 0, 0, 0));
	ImPlot::PushStyleColor(ImPlotCol_AxisBgHovered, IM_COL32(0, 0, 0, 0));

	if (ImPlot::BeginPlot(_S("CPU time"), plot_size, ImPlotFlags_CanvasOnly))
	{
		auto col = ImPlot::GetColormapColor(0);

		ImPlot::SetupAxes(nullptr, _S("CPU time [ms]"), ImPlotAxisFlags_NoDecorations, 0);
		ImPlot::SetupAxesLimits(0, cpu_time.size() - 1, min_v, max_v, ImGuiCond_Always);
		ImPlot::SetNextLineStyle(col);
		ImPlot::SetNextFillStyle(col, 0.25);
		ImPlot::PlotLine(_S("CPU time"), cpu_time.data(), cpu_time.size(), 1, 0, ImPlotLineFlags_Shaded, offset);
		ImPlot::EndPlot();
	}

	ImGui::SameLine();

	if (ImPlot::BeginPlot(_S("GPU time"), plot_size, ImPlotFlags_CanvasOnly))
	{
		auto col = ImPlot::GetColormapColor(1);

		ImPlot::SetupAxes(nullptr, _S("GPU time [ms]"), ImPlotAxisFlags_NoDecorations, 0);
		ImPlot::SetupAxesLimits(0, gpu_time.size() - 1, min_v, max_v, ImGuiCond_Always);
		ImPlot::SetNextLineStyle(col);
		ImPlot::SetNextFillStyle(col, 0.25);
		ImPlot::PlotLine(_S("GPU time"), gpu_time.data(), gpu_time.size(), 1, 0, ImPlotLineFlags_Shaded, offset);
		ImPlot::EndPlot();
	}
	ImPlot::PopStyleColor(5);

	ImGui::PopStyleVar();
}
#endif

void scenes::lobby::gui_about()
{
	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	CenterTextH(std::string("WiVRn ") + wivrn::git_version);
	ImGui::PopFont();

	ImGui::Dummy(ImVec2(0, 60));

	float win_width = ImGui::GetWindowSize().x;
	ImGui::SetCursorPosX(win_width / 4);

	ImGui::Image(about_picture, {win_width / 2, win_width / 2});
}

void scenes::lobby::gui_first_run()
{
	float win_width = ImGui::GetWindowSize().x;
	auto & config = application::get_config();
	const ImGuiStyle & style = ImGui::GetStyle();

	struct item
	{
		feature f;
		std::string text;
		bool supported;
	};

	std::array optional_features{
	        item{
	                .f = feature::microphone,
	                .text = _S("Enable the microphone?"),
	                .supported = true,
	        },
	        item{
	                .f = feature::eye_gaze,
	                .text = _S("Enable eye tracking?"),
	                .supported = application::get_eye_gaze_supported(),
	        },
	        item{
	                .f = feature::face_tracking,
	                .text = _S("Enable face tracking?"),
	                .supported = not std::holds_alternative<std::monostate>(face_tracker),
	        },
	        item{
	                .f = feature::body_tracking,
	                .text = _S("Enable body tracking?"),
	                .supported = system.body_tracker_supported() != xr::body_tracker_type::none,
	        },
	};

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {20, 40});

	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	CenterTextH(_("Welcome to WiVRn"));
	ImGui::PopFont();

	while (optional_feature_index < optional_features.size() and
	       (not optional_features[optional_feature_index].supported or
	        config.check_feature(optional_features[optional_feature_index].f)))

		optional_feature_index++;

	if (optional_feature_index == optional_features.size())
	{
		current_tab = tab::server_list;
		config.first_run = false;
		config.save();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);

	CenterTextH(optional_features[optional_feature_index].text);

	float button_width = constants::style::button_size.x;
	float buttons_width = 2 * button_width + style.ItemSpacing.x;
	ImGui::SetCursorPosX((win_width - buttons_width) / 2);

	if (ImGui::Button(_S("Yes"), constants::style::button_size))
	{
		config.set_feature(optional_features[optional_feature_index].f, true);
	}
	imgui_ctx->vibrate_on_hover();

	ImGui::SameLine();
	if (ImGui::Button(_S("No"), constants::style::button_size))
	{
		config.set_feature(optional_features[optional_feature_index].f, false);
		optional_feature_index++;
	}
	imgui_ctx->vibrate_on_hover();
	ImGui::PopStyleVar(2); // ImGuiStyleVar_ItemSpacing

	if (optional_feature_index == optional_features.size())
	{
		current_tab = tab::server_list;
		config.first_run = false;
		config.save();
	}
}

void scenes::lobby::gui_licenses()
{
	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	ImGui::Text("%s", _("Licenses").c_str());
	ImGui::PopFont();

	const auto components = {"WiVRn", "FontAwesome", "openxr-loader", "simdjson"};
	if (not license)
	{
		selected_item = *components.begin();
		try
		{
			license = std::make_unique<asset>(std::filesystem::path("licenses") / selected_item);
		}
		catch (...)
		{
			spdlog::warn("No license file for {}", selected_item);
		}
	}
	if (ImGui::BeginCombo("##component", selected_item.c_str()))
	{
		for (const auto & component: components)
		{
			try
			{
				auto current = std::make_unique<asset>(std::filesystem::path("licenses") / component);
				if (ImGui::Selectable(component, component == selected_item, ImGuiSelectableFlags_SelectOnRelease))
				{
					selected_item = component;
					license = std::move(current);
				}
			}
			catch (...)
			{
				spdlog::debug("No license file for {}", component);
			}
		}
		ImGui::EndCombo();
	}
	imgui_ctx->vibrate_on_hover();
	if (license)
		ImGui::TextUnformatted((const char *)license->data(), (const char *)license->data() + license->size());
}

static bool RadioButtonWithoutCheckBox(const std::string & label, bool active, ImVec2 size_arg)
{
	ImGuiWindow * window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext & g = *GImGui;
	const ImGuiStyle & style = g.Style;
	const ImGuiID id = window->GetID(label.c_str());
	const ImVec2 label_size = ImGui::CalcTextSize(label.c_str(), NULL, true);

	const ImVec2 pos = window->DC.CursorPos;

	ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

	const ImRect bb(pos, pos + size);
	ImGui::ItemSize(bb, style.FramePadding.y);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered, held;
	bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

	ImGuiCol_ col;
	if ((held && hovered) || active)
		col = ImGuiCol_ButtonActive;
	else if (hovered)
		col = ImGuiCol_ButtonHovered;
	else
		col = ImGuiCol_Button;

	ImGui::RenderNavHighlight(bb, id);
	ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(col), true, style.FrameRounding);

	ImVec2 TextAlign{0, 0.5f};
	ImGui::RenderTextClipped(bb.Min + style.FramePadding, bb.Max - style.FramePadding, label.c_str(), NULL, &label_size, TextAlign, &bb);

	IMGUI_TEST_ENGINE_ITEM_INFO(id, label.c_str(), g.LastItemData.StatusFlags);
	return pressed;
}

template <typename T>
static bool RadioButtonWithoutCheckBox(const std::string & label, T * v, T v_button, ImVec2 size_arg)
{
	const bool pressed = RadioButtonWithoutCheckBox(label, *v == v_button, size_arg);
	if (pressed)
		*v = v_button;
	return pressed;
}

static auto face_weights()
{
	using weights = decltype(wivrn::from_headset::tracking::fb_face2{}.weights);
	using item = std::pair<const char *, std::array<float, XR_FACE_EXPRESSION2_COUNT_FB>>;
	std::vector<item> res;

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_SMILE, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_TIRED, face);
	}
	{
		weights face{};
		face[XR_FACE_EXPRESSION2_JAW_DROP_FB] = 1;
		res.emplace_back(ICON_FA_FACE_SURPRISE, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_EYES_CLOSED_L_FB] = 1;
		res.emplace_back(ICON_FA_FACE_SMILE_WINK, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_EYES_CLOSED_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_SMILE_WINK, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_SMILE_BEAM, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_EYES_LOOK_DOWN_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_EYES_LOOK_DOWN_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_SAD_CRY, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_EYES_LOOK_UP_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_EYES_LOOK_UP_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_ROLLING_EYES, face);
	}

	{
		weights face{};
		res.emplace_back(ICON_FA_FACE_MEH, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_EYES_CLOSED_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_JAW_DROP_FB] = 1;
		res.emplace_back(ICON_FA_FACE_LAUGH_WINK, face);
	}
	{
		weights face{};
		face[XR_FACE_EXPRESSION2_EYES_CLOSED_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_JAW_DROP_FB] = 1;
		res.emplace_back(ICON_FA_FACE_LAUGH_WINK, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_JAW_DROP_FB] = 1;
		res.emplace_back(ICON_FA_FACE_LAUGH_SQUINT, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_JAW_DROP_FB] = 1;
		res.emplace_back(ICON_FA_FACE_LAUGH, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_INNER_BROW_RAISER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_INNER_BROW_RAISER_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_GRIN_WIDE, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_EYES_CLOSED_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_TONGUE_OUT_FB] = 1;
		res.emplace_back(ICON_FA_FACE_GRIN_TONGUE_WINK, face);
	}
	{
		weights face{};
		face[XR_FACE_EXPRESSION2_EYES_CLOSED_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_TONGUE_OUT_FB] = 1;
		res.emplace_back(ICON_FA_FACE_GRIN_TONGUE_WINK, face);
	}
	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LID_TIGHTENER_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_TONGUE_OUT_FB] = 1;
		res.emplace_back(ICON_FA_FACE_GRIN_TONGUE_SQUINT, face);
	}
	{
		weights face{};
		face[XR_FACE_EXPRESSION2_TONGUE_OUT_FB] = 1;
		res.emplace_back(ICON_FA_FACE_GRIN_TONGUE, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_CORNER_PULLER_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_JAW_DROP_FB] = 0.5;
		res.emplace_back(ICON_FA_FACE_GRIN, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_CHIN_RAISER_T_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_STRETCHER_L_FB] = 0.5;
		face[XR_FACE_EXPRESSION2_LIP_STRETCHER_R_FB] = 0.5;
		res.emplace_back(ICON_FA_FACE_GRIMACE, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_JAW_DROP_FB] = 0.5;
		res.emplace_back(ICON_FA_FACE_FROWN_OPEN, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_CORNER_DEPRESSOR_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_FROWN, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_UPPER_LID_RAISER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_UPPER_LID_RAISER_R_FB] = 1;
		face[XR_FACE_EXPRESSION2_LIP_TIGHTENER_L_FB] = 0.5;
		face[XR_FACE_EXPRESSION2_LIP_TIGHTENER_R_FB] = 0.5;
		res.emplace_back(ICON_FA_FACE_FLUSHED, face);
	}

	{
		weights face{};
		face[XR_FACE_EXPRESSION2_BROW_LOWERER_L_FB] = 1;
		face[XR_FACE_EXPRESSION2_BROW_LOWERER_R_FB] = 1;
		res.emplace_back(ICON_FA_FACE_ANGRY, face);
	}

	return res;
}

static const char * get_face_icon(XrTime predicted_display_time, xr::face_tracker & face_tracker)
{
	static const auto w = face_weights();
	wivrn::from_headset::tracking::fb_face2 expression;
	const char * result = nullptr;
	std::visit(utils::overloaded{
	                   [](std::monostate &) {},
	                   [&](xr::htc_face_tracker &) { result = ICON_FA_FACE_SMILE_WINK; },
	                   [&](auto & ft) {
		                   ft.get_weights(predicted_display_time, expression);
	                   },
	           },
	           face_tracker);

	if (result)
		return result;

	if (not expression.is_valid)
		return ICON_FA_FACE_MEH;

	return std::ranges::min_element(w, std::ranges::less(), [&](const auto & p) {
		       float res = 0;
		       for (size_t i = 0; i < XR_FACE_EXPRESSION2_COUNT_FB; ++i)
		       {
			       float d = expression.weights[i] - p.second[i];
			       res += d * d;
		       }
		       return res;
	       })
	        ->first;
}

void scenes::lobby::draw_features_status(XrTime predicted_display_time)
{
	const float win_width = ImGui::GetContentRegionAvail().x;
	float text_width = 0;
	auto & config = application::get_config();

	struct item
	{
		feature f;
		std::string tooltip_enabled;
		std::string tooltip_disabled;
		const char * icon_enabled;
		const char * icon_disabled = ICON_FA_SLASH;
		bool enabled;
		float w;
	};
	std::vector<item> items;

	items.push_back({
	        .f = feature::microphone,
	        .tooltip_enabled = _("Microphone is enabled"),
	        .tooltip_disabled = _("Microphone is disabled"),
	        .icon_enabled = ICON_FA_MICROPHONE,
	        .icon_disabled = ICON_FA_MICROPHONE_SLASH,
	});

	if (system.hand_tracking_supported())
	{
		items.push_back({
		        .f = feature::hand_tracking,
		        .tooltip_enabled = _("Hand tracking is enabled"),
		        .tooltip_disabled = _("Hand tracking is disabled"),
		        .icon_enabled = ICON_FA_HAND,
		});
	}

	if (application::get_eye_gaze_supported())
	{
		items.push_back({
		        .f = feature::eye_gaze,
		        .tooltip_enabled = _("Eye tracking is enabled"),
		        .tooltip_disabled = _("Eye tracking is disabled"),
		        .icon_enabled = ICON_FA_EYE,
		        .icon_disabled = ICON_FA_EYE_SLASH,
		});
	}

	if (not std::holds_alternative<std::monostate>(face_tracker))
	{
		items.push_back({
		        .f = feature::face_tracking,
		        .tooltip_enabled = _("Face tracking is enabled"),
		        .tooltip_disabled = _("Face tracking is disabled"),
		        .icon_enabled = get_face_icon(predicted_display_time, face_tracker),
		        .icon_disabled = ICON_FA_FACE_MEH_BLANK,
		});
	}

	if (system.body_tracker_supported() != xr::body_tracker_type::none)
	{
		items.push_back({
		        .f = feature::body_tracking,
		        .tooltip_enabled = _("Body tracking is enabled"),
		        .tooltip_disabled = _("Body tracking is disabled"),
		        .icon_enabled = ICON_FA_PERSON,
		});
	}

	// Get statuses
	for (auto & i: items)
	{
		i.enabled = config.check_feature(i.f);
		i.w = ImGui::CalcTextSize(i.enabled ? i.icon_enabled : i.icon_disabled).x;
		text_width += i.w;
	}
	const ImGuiStyle & style = ImGui::GetStyle();
	text_width += items.size() * style.FramePadding.x * 2;

	// New server button
	if (ImGui::Button(_S("Add server")) && !ImGui::IsPopupOpen("add or edit server"))
		ImGui::OpenPopup("add or edit server");
	imgui_ctx->vibrate_on_hover();
	ImGui::SameLine();

	// Enabled features
	ImGui::SetCursorPosX((win_width - text_width) / 2);
	for (auto & i: items)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, i.w / 2);
		ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32_BLACK_TRANS);
		ImGui::PushStyleColor(ImGuiCol_Text, i.enabled ? ImGui::GetColorU32(ImGuiCol_Text) : IM_COL32(255, 0, 0, 255));
		if (&i != &items.front())
			ImGui::SameLine();
		auto pos = ImGui::GetCursorPos();
		if (ImGui::Button(fmt::format("{}##{}", i.enabled ? i.icon_enabled : i.icon_disabled, i.icon_enabled).c_str()))
		{
			// button doesn't alter the bool
			config.set_feature(i.f, not i.enabled);
		}

		imgui_ctx->vibrate_on_hover();
		if (ImGui::IsItemHovered())
			imgui_ctx->tooltip(i.enabled ? i.tooltip_enabled : i.tooltip_disabled);

		if (i.icon_disabled == std::string_view(ICON_FA_SLASH) and not i.enabled)
		{
			auto save = ImGui::GetCurrentWindow()->DC;
			ImGui::SetCursorPos(pos + ImGui::GetStyle().FramePadding);
			ImGui::Text("%s", i.icon_enabled);
			ImGui::GetCurrentWindow()->DC = save;
		}
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar();
	}

	float PrevLineSize = ImGui::GetCurrentWindow()->DC.PrevLineSize.y;
	float PrevLineTextBaseOffset = ImGui::GetCurrentWindow()->DC.PrevLineTextBaseOffset;

#ifdef __ANDROID__
	// Battery status
	auto status = get_battery_status();

	const char * battery_icon = nullptr;
	if (status.charge)
	{
		int icon_nr;

		if (status.charging)
		{
			if (*status.charge > 0.995)
				icon_nr = 5;
			else
				icon_nr = instance.now() / 500'000'000 % 5;
		}
		else
			icon_nr = std::round((*status.charge) * 4);

		switch (icon_nr)
		{
			case 0:
				battery_icon = ICON_FA_BATTERY_EMPTY;
				break;
			case 1:
				battery_icon = ICON_FA_BATTERY_QUARTER;
				break;
			case 2:
				battery_icon = ICON_FA_BATTERY_HALF;
				break;
			case 3:
				battery_icon = ICON_FA_BATTERY_THREE_QUARTERS;
				break;
			case 4:
				battery_icon = ICON_FA_BATTERY_FULL;
				break;
			case 5:
				battery_icon = ICON_FA_PLUG;
		}

		ImGui::SameLine();

		// Always use the longest width for layout
		float max_battery_width = ImGui::CalcTextSize(ICON_FA_BATTERY_FULL "100%").x;
		ImVec4 battery_color = ImGui::GetStyleColorVec4(ImGuiCol_Text);

		if (*status.charge < 0.2)
			battery_color = ImVec4(1, 0, 0, 1);

		ImGui::SetCursorPosX(win_width - max_battery_width - style.WindowPadding.x);
		ImGui::TextColored(battery_color, "%s %d%%", battery_icon, (int)std::round(*status.charge * 100));
	}
#endif

	ImGui::Dummy({0, 15});
}

void scenes::lobby::gui_keyboard()
{
	keyboard.display(*imgui_ctx);

	auto & config = application::get_config();

	if (keyboard.get_layout() != config.virtual_keyboard_layout)
	{
		config.virtual_keyboard_layout = keyboard.get_layout();
		config.save();
	}
}

static bool is_gui_visible(imgui_context & ctx, XrTime predicted_display_time)
{
	// Get the GUI position in the view reference frame
	if (auto pos = application::locate_controller(application::space(xr::spaces::world), application::space(xr::spaces::view), predicted_display_time))
	{
		glm::vec3 view_gui_position = pos->first + pos->second * ctx.layers()[0].position;

		float gui_distance = glm::length(view_gui_position);
		glm::vec3 direction = view_gui_position / gui_distance;

		if (view_gui_position.z > 0 or view_gui_position.z < -1.5)
			return false;

		if (std::abs(direction.x) > 0.8)
			return false;

		if (std::abs(direction.y) > 0.8)
			return false;

		return true;
	}

	return true;
}

std::vector<std::pair<int, XrCompositionLayerQuad>> scenes::lobby::draw_gui(XrTime predicted_display_time)
{
	imgui_ctx->new_frame(predicted_display_time);
	ImGuiStyle & style = ImGui::GetStyle();

	const float TabWidth = 300;

	if (ImGui::GetIO().WantTextInput)
	{
		ImGui::SetNextWindowPos(imgui_ctx->layers()[2].vp_center(), ImGuiCond_Always, {0.5, 0.5});
		gui_keyboard();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 30);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 8, 8, 224));

	ImGui::SetNextWindowPos(imgui_ctx->layers()[0].vp_center(), ImGuiCond_Always, {0.5, 0.5});

	if (current_tab == tab::first_run)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, constants::style::window_padding * 2);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, constants::style::window_rounding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, constants::style::window_border_size);
		ImGui::Begin("WiVRn", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
		gui_first_run();
		ImGui::End();
		ImGui::PopStyleVar(4); // ImGuiStyleVar_FrameRounding, ImGuiStyleVar_WindowPadding, ImGuiStyleVar_WindowRounding, ImGuiStyleVar_WindowBorderSize
	}
	else if (current_tab == tab::connected)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 20});
		ImGui::SetNextWindowSize({1400, 900});
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 10});
		ImGui::Begin("WiVRn", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
		gui_connected(predicted_display_time);
		ImGui::End();
		ImGui::PopStyleVar(3); // ImGuiStyleVar_WindowPadding, ImGuiStyleVar_FrameRounding, ImGuiStyleVar_FramePadding
	}
	else
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
		ImGui::SetNextWindowSize({1400, 900});
		ImGui::Begin("WiVRn", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
		ImGui::SetCursorPos({TabWidth + 20, 0});

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 20});
		ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0), 0);
		ImGui::SetCursorPosY(20);

		switch (current_tab)
		{
			case tab::first_run:
			case tab::connected:
				__builtin_unreachable();

			case tab::server_list:
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 10});
				draw_features_status(predicted_display_time);
				ImGui::PopStyleVar();

				gui_server_list();
				break;

			case tab::settings:
				gui_settings();
				break;

			case tab::post_processing:
				gui_post_processing();
				break;

#if WIVRN_CLIENT_DEBUG_MENU
			case tab::debug:
				gui_debug();
				break;
#endif

			case tab::about:
				gui_about();
				break;

			case tab::licenses:
				gui_licenses();
				break;

			case tab::exit:
				application::pop_scene();
				break;
		}

		if (current_tab != last_current_tab)
		{
			last_current_tab = current_tab;
			ImGui::SetScrollY(0);
		}

		ImGui::Dummy(ImVec2(0, 20));

		ScrollWhenDragging();
		ImGui::EndChild();
		ImGui::PopStyleVar(2); // ImGuiStyleVar_FrameRounding, ImGuiStyleVar_WindowPadding

		ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255));
		ImGui::SetCursorPos(style.WindowPadding);
		{
			ImGui::BeginChild("Tabs", {TabWidth, ImGui::GetContentRegionMax().y - ImGui::GetWindowContentRegionMin().y});

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
			RadioButtonWithoutCheckBox(ICON_FA_COMPUTER "  " + _("Server list"), &current_tab, tab::server_list, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_GEARS "  " + _("Settings"), &current_tab, tab::settings, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_WAND_MAGIC_SPARKLES "  " + _("Post-processing"), &current_tab, tab::post_processing, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

#if WIVRN_CLIENT_DEBUG_MENU
			RadioButtonWithoutCheckBox(ICON_FA_BUG_SLASH "  " + _("Debug"), &current_tab, tab::debug, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();
#endif

			ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - 3 * ImGui::GetCurrentContext()->FontSize - 6 * style.FramePadding.y - 2 * style.ItemSpacing.y - style.WindowPadding.y);
			RadioButtonWithoutCheckBox(ICON_FA_CIRCLE_INFO "  " + _("About"), &current_tab, tab::about, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_SCALE_BALANCED "  " + _("Licenses"), &current_tab, tab::licenses, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			RadioButtonWithoutCheckBox(ICON_FA_DOOR_OPEN "  " + _("Exit"), &current_tab, tab::exit, {TabWidth, 0});
			imgui_ctx->vibrate_on_hover();

			ImGui::PopStyleVar(); // ImGuiStyleVar_FramePadding
			ImGui::EndChild();
		}
		ImGui::PopStyleColor(); // ImGuiCol_ChildBg
		ImGui::End();
		ImGui::PopStyleVar(); // ImGuiStyleVar_WindowPadding
	}

	ImGui::PopStyleColor(); // ImGuiCol_WindowBg
	ImGui::PopStyleVar();   // ImGuiStyleVar_ScrollbarSize

	if (not is_gui_visible(*imgui_ctx, predicted_display_time))
	{
		if (system.hand_tracking_supported())
			display_recentering_tip(*imgui_ctx, _("Press the grip button or put your palm up\nto move the main window"));
		else
			display_recentering_tip(*imgui_ctx, _("Press the grip button to move the main window"));
	}

#if WIVRN_CLIENT_DEBUG_MENU
	{
		input->offset[xr::spaces::grip_left].first = offset_position;
		input->offset[xr::spaces::grip_right].first = offset_position;

		glm::quat qx = glm::quat(std::cos(offset_orientation.x * M_PI / 360), sin(offset_orientation.x * M_PI / 360), 0, 0);
		glm::quat qy = glm::quat(std::cos(offset_orientation.y * M_PI / 360), 0, sin(offset_orientation.y * M_PI / 360), 0);
		glm::quat qz = glm::quat(std::cos(offset_orientation.z * M_PI / 360), 0, 0, sin(offset_orientation.z * M_PI / 360));
		glm::quat q = qz * qy * qx;

		input->offset[xr::spaces::grip_left].second = q;
		input->offset[xr::spaces::grip_right].second = q;

		input->offset[xr::spaces::aim_left].first.z = ray_offset;
		input->offset[xr::spaces::aim_right].first.z = ray_offset;
	}
#endif

	return imgui_ctx->end_frame();
}
