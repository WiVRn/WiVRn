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

#include "application.h"
#include "asset.h"
#include "configuration.h"
#include "constants.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "lobby.h"
#include "stream.h"
#include "version.h"
#include <glm/gtc/quaternion.hpp>
#include <ranges>
#include <spdlog/fmt/fmt.h>
#include <utils/strings.h>

#include "IconsFontAwesome6.h"

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

static void display_recentering_tip(imgui_context & ctx, const std::string & tip)
{
	ImGui::PushFont(ctx.large_font);
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

void scenes::lobby::tooltip(std::string_view text)
{
	// FIXME: this is incorrect if we use the docking branch of imgui
	ImGuiViewport * viewport = ImGui::GetMainViewport();
	auto & layer = imgui_ctx->layer(ImGui::GetMousePos());
	auto pos_backup = viewport->Pos;
	auto size_backup = viewport->Size;
	viewport->Pos = ImVec2(layer.vp_origin.x, layer.vp_origin.y);
	viewport->Size = ImVec2(layer.vp_size.x, layer.vp_size.y);

	ImVec2 pos{
	        (ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) / 2,
	        ImGui::GetItemRectMin().y - constants::style::tooltip_distance,
	};

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, constants::style::tooltip_padding);

	// Clamp position to avoid overflowing on the right
	auto & style = ImGui::GetStyle();
	const ImVec2 text_size = ImGui::CalcTextSize(text.data(), text.data() + text.size(), true);
	const ImVec2 size = {text_size.x + style.WindowPadding.x * 2.0f, text_size.y + style.WindowPadding.y * 2.0f};
	pos.x = std::min(pos.x, viewport->Pos.x + viewport->Size.x - size.x / 2);

	ImGui::SetNextWindowPos(pos, ImGuiCond_Always, {0.5, 1});
	if (ImGui::BeginTooltip())
	{
		ImGui::PushStyleColor(ImGuiCol_Text, 0xffffffff);
		ImGui::TextUnformatted(text.data(), text.data() + text.size());
		ImGui::PopStyleColor();
		ImGui::EndTooltip();
	}

	ImGui::PopStyleVar();

	viewport->Pos = pos_backup;
	viewport->Size = size_backup;
}

void scenes::lobby::vibrate_on_hover()
{
	if (ImGui::IsItemHovered())
		hovered_item = ImGui::GetItemID();
}

void scenes::lobby::gui_connecting()
{
	using constants::style::button_size;

	std::string status;
	if (next_scene)
	{
		if (next_scene->current_state() == scenes::stream::state::stalled)
			status = _("Video stream interrupted");
		else if (server_name == "")
			status = fmt::format(_F("Connection ready\nStart a VR application on your computer"));
		else
			status = fmt::format(_F("Connection ready\nStart a VR application on {}"), server_name);
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

	std::string button_label;

	if (async_error)
		button_label = _("Close");
	else
		button_label = _("Cancel");

	ImGui::Dummy({1000, 1});

	ImGui::PushFont(imgui_ctx->large_font);
	if (server_name == "")
		CenterTextH(fmt::format(_F("Connection")));
	else
		CenterTextH(fmt::format(_F("Connection to {}"), server_name));
	ImGui::PopFont();

	// ImGui::TextWrapped("%s", status.first.c_str());
	ImGui::Text("%s", status.c_str());

	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - button_size.x - ImGui::GetStyle().WindowPadding.x);

	if (ImGui::Button(button_label.c_str(), button_size))
	{
		async_session.cancel();

		next_scene.reset();

		ImGui::CloseCurrentPopup();
	}
	vibrate_on_hover();
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
	ImGui::InputTextEx("##Name", nullptr, add_server_window_prettyname, sizeof(add_server_window_prettyname), {ImGui::GetContentRegionAvail().x, 0}, 0);
	vibrate_on_hover();

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
	ImGui::Text("%s", _S("Address"));

	ImGui::TableNextColumn();
	ImGui::InputTextEx("##Hostname", nullptr, add_server_window_hostname, sizeof(add_server_window_hostname), {ImGui::GetContentRegionAvail().x, 0}, 0);
	vibrate_on_hover();

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().FramePadding.y);
	ImGui::Text("%s", _S("Port"));

	ImGui::TableNextColumn();
	ImGui::InputInt("##Port", &add_server_window_port, 1, 1, ImGuiInputTextFlags_CharsDecimal);
	vibrate_on_hover();

	ImGui::EndTable();

	ImGui::Checkbox(_S("TCP only"), &add_server_tcp_only);
	vibrate_on_hover();

	auto top_left = ImGui::GetWindowContentRegionMin();
	auto bottom_right = ImGui::GetWindowContentRegionMax();

	ImGui::SetCursorPosX(top_left.x);
	if (ImGui::Button(_S("Cancel"), button_size))
	{
		current_tab = tab::server_list;
		strcpy(add_server_window_prettyname, "");
		strcpy(add_server_window_hostname, "");
		add_server_window_port = wivrn::default_port;
		add_server_tcp_only = false;
		ImGui::CloseCurrentPopup();
	}
	vibrate_on_hover();

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
		config.servers.emplace("manual-" + data.service.name, data);
		config.save();

		strcpy(add_server_window_prettyname, "");
		strcpy(add_server_window_hostname, "");
		add_server_window_port = wivrn::default_port;
		add_server_tcp_only = false;
		ImGui::CloseCurrentPopup();
	}
	vibrate_on_hover();

	ImGui::PopStyleVar(4); // ImGuiStyleVar_FrameRounding, ImGuiStyleVar_ItemSpacing, ImGuiStyleVar_CellPadding, ImGuiStyleVar_FramePadding
}

void scenes::lobby::gui_server_list()
{
	using constants::style::button_size;

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
		ImGui::PushFont(imgui_ctx->large_font);
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
			vibrate_on_hover();
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
		vibrate_on_hover();

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			if (!data.compatible && !data.manual)
				tooltip(_("Incompatible server version"));
			else if (!data.visible && !data.manual)
				tooltip(_("Server not available"));
		}

		ImGui::PopStyleColor(3);
		ImGui::EndDisabled();

		button_position.x -= button_size.x + style.WindowPadding.x + 20;
		if (data.manual)
		{
			ImGui::SetCursorPos(button_position);
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.00f));

			if (ImGui::Button((_("Remove") + "##" + cookie).c_str(), button_size))
				cookie_to_remove = cookie;
			vibrate_on_hover();

			ImGui::PopStyleColor(3);
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
	if ((async_session.valid() || next_scene) && !ImGui::IsPopupOpen("connecting"))
		ImGui::OpenPopup("connecting");

	const auto & popup_layer = imgui_ctx->layers()[1];
	const glm::vec2 popup_layer_center = popup_layer.vp_origin + popup_layer.vp_size / 2;
	ImGui::SetNextWindowPos({popup_layer_center.x, popup_layer_center.y}, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, constants::style::window_padding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, constants::style::window_rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, constants::style::window_border_size);
	if (ImGui::BeginPopupModal("connecting", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		gui_connecting();
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowSize({800, 0});
	ImGui::SetNextWindowPos({popup_layer_center.x, popup_layer_center.y}, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("add server", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
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
		float current = session.get_current_refresh_rate();
		if (not refresh_rates.empty())
		{
			if (ImGui::BeginCombo(_S("Refresh rate"), fmt::format("{}", current).c_str()))
			{
				for (float rate: refresh_rates)
				{
					if (ImGui::Selectable(fmt::format("{}", rate).c_str(), rate == current, ImGuiSelectableFlags_SelectOnRelease) and rate != current)
					{
						session.set_refresh_rate(rate);
						config.preferred_refresh_rate = rate;
						config.save();
					}
				}
				ImGui::EndCombo();
			}
			vibrate_on_hover();
		}
	}

	{
		const auto available_scales = {0.5, 0.6, 0.7, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8, 1.9, 2.0};
		const auto current = config.resolution_scale;
		const auto width = stream_view.recommendedImageRectWidth;
		const auto height = stream_view.recommendedImageRectHeight;
		if (ImGui::BeginCombo(_S("Resolution scale"), fmt::format(_F("{} - {}x{} per eye"), current, (int)(width * current), (int)(height * current)).c_str()))
		{
			for (float scale: available_scales)
			{
				if (ImGui::Selectable(fmt::format(_F("{} - {}x{} per eye"), scale, (int)(width * scale), (int)(height * scale)).c_str(), scale == current, ImGuiSelectableFlags_SelectOnRelease) and scale != current)
				{
					config.resolution_scale = scale;
					config.save();
				}
			}
			ImGui::EndCombo();
		}
		vibrate_on_hover();
	}

	{
		bool enabled = config.check_feature(feature::microphone);
		if (ImGui::Checkbox(_S("Enable microphone"), &enabled))
		{
			config.set_feature(feature::microphone, enabled);
			config.save();
		}
		vibrate_on_hover();
	}
	{
		ImGui::BeginDisabled(not application::get_hand_tracking_supported());
		bool enabled = config.check_feature(feature::hand_tracking);
		if (ImGui::Checkbox(_S("Enable hand tracking"), &enabled))
		{
			config.set_feature(feature::hand_tracking, enabled);
			config.save();
		}
		vibrate_on_hover();
		ImGui::EndDisabled();
	}
	{
		ImGui::BeginDisabled(not application::get_eye_gaze_supported());
		bool enabled = config.check_feature(feature::eye_gaze);
		if (ImGui::Checkbox(_S("Enable eye tracking"), &enabled))
		{
			config.set_feature(feature::eye_gaze, enabled);
			config.save();
		}
		vibrate_on_hover();
		ImGui::EndDisabled();
	}
	{
		ImGui::BeginDisabled(not application::get_fb_face_tracking2_supported());
		bool enabled = config.check_feature(feature::face_tracking);
		if (ImGui::Checkbox(_S("Enable face tracking"), &enabled))
		{
			config.set_feature(feature::face_tracking, enabled);
			config.save();
		}
		ImGui::EndDisabled();
		vibrate_on_hover();
	}

	ImGui::BeginDisabled(passthrough_supported == xr::system::passthrough_type::no_passthrough);
	if (ImGui::Checkbox(_S("Enable video passthrough in lobby"), &config.passthrough_enabled))
	{
		setup_passthrough();
		config.save();
	}
	vibrate_on_hover();
	ImGui::EndDisabled();

	if (ImGui::Checkbox(_S("Show performance metrics"), &config.show_performance_metrics))
		config.save();
	vibrate_on_hover();
	if (ImGui::IsItemHovered())
		tooltip(_("Overlay can be toggled by pressing both thumbsticks"));

	ImGui::PopStyleVar();

	if (config.show_performance_metrics)
	{
		float win_width = ImGui::GetWindowSize().x;
		float win_height = ImGui::GetWindowSize().y;

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

		if (ImPlot::BeginPlot(_S("CPU time"), plot_size, ImPlotFlags_CanvasOnly | ImPlotFlags_NoChild))
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

		if (ImPlot::BeginPlot(_S("GPU time"), plot_size, ImPlotFlags_CanvasOnly | ImPlotFlags_NoChild))
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
	}
}

#if WIVRN_CLIENT_DEBUG_MENU
void scenes::lobby::gui_debug()
{
	ImGui::GetIO().ConfigDragClickToInputText = true;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 20));

	ImGui::Checkbox("Display debug axes", &display_debug_axes);
	vibrate_on_hover();

	if (display_debug_axes)
	{
		ImGui::Checkbox("Display grip instead of aim", &display_grip_instead_of_aim);
		vibrate_on_hover();
	}

	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("##offset x", &offset_position.x, 0.0001);
	vibrate_on_hover();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("##offset y", &offset_position.y, 0.0001);
	vibrate_on_hover();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("Position", &offset_position.z, 0.0001);
	vibrate_on_hover();

	ImGui::SameLine();
	if (ImGui::Button("Reset##position"))
		offset_position = {0, 0, 0};
	vibrate_on_hover();

	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("##offset roll", &offset_orientation.x, 0.01);
	vibrate_on_hover();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("##offset pitch", &offset_orientation.y, 0.01);
	vibrate_on_hover();

	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("Rotation", &offset_orientation.z, 0.01);
	vibrate_on_hover();

	ImGui::SameLine();
	if (ImGui::Button("Reset##orientation"))
		offset_orientation = {0, 0, 0};
	vibrate_on_hover();

	ImGui::SetNextItemWidth(140);
	ImGui::DragFloat("Ray offset", &ray_offset, 0.0001);
	vibrate_on_hover();

	ImGui::PopStyleVar();
}
#endif

void scenes::lobby::gui_about()
{
	ImGui::PushFont(imgui_ctx->large_font);
	CenterTextH(std::string("WiVRn ") + wivrn::git_version);
	ImGui::PopFont();

	ImGui::Dummy(ImVec2(0, 60));

	float win_width = ImGui::GetWindowSize().x;
	ImGui::SetCursorPosX(win_width / 4);

	ImGui::Image(about_picture, {win_width / 2, win_width / 2});
}

void scenes::lobby::gui_licenses()
{
	ImGui::PushFont(imgui_ctx->large_font);
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
	vibrate_on_hover();
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

static void ScrollWhenDraggingOnVoid()
{
	ImVec2 delta{0.0f, -ImGui::GetIO().MouseDelta.y};

	// https://github.com/ocornut/imgui/issues/3379#issuecomment-1678718752
	ImGuiContext & g = *ImGui::GetCurrentContext();
	ImGuiWindow * window = g.CurrentWindow;
	bool hovered = false;
	bool held = false;
	static bool held_prev = false;

	// Don't drag for the first frame because the current controller might have just changed and have a large delta

	ImGuiID id = window->GetID("##scrolldraggingoverlay");
	ImGui::KeepAliveID(id);
	if (g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
		ImGui::ButtonBehavior(window->Rect(), id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
	if (held and held_prev and delta.x != 0.0f)
		ImGui::SetScrollX(window, window->Scroll.x + delta.x);
	if (held and held_prev and delta.y != 0.0f)
		ImGui::SetScrollY(window, window->Scroll.y + delta.y);

	held_prev = held;
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

static const char * get_face_icon(XrTime predicted_display_time)
{
	static const auto w = face_weights();
	wivrn::from_headset::tracking::fb_face2 expression;
	application::get_fb_face_tracker2().get_weights(predicted_display_time, expression);

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

	if (application::get_hand_tracking_supported())
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

	if (application::get_fb_face_tracking2_supported())
	{
		items.push_back({
		        .f = feature::face_tracking,
		        .tooltip_enabled = _("Face tracking is enabled"),
		        .tooltip_disabled = _("Face tracking is disabled"),
		        .icon_enabled = get_face_icon(predicted_display_time),
		        .icon_disabled = ICON_FA_FACE_MEH_BLANK,
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
	if (ImGui::Button(_S("Add server")) && !ImGui::IsPopupOpen("add server"))
		ImGui::OpenPopup("add server");
	vibrate_on_hover();
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
		if (ImGui::Button(i.enabled ? i.icon_enabled : i.icon_disabled))
		{
			// button doesn't alter the bool
			config.set_feature(i.f, not i.enabled);
			config.save();
		}

		vibrate_on_hover();
		if (ImGui::IsItemHovered())
			tooltip(i.enabled ? i.tooltip_enabled : i.tooltip_disabled);

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
	auto status = battery_tracker.get();

	const char * battery_icon = nullptr;
	if (status.charge)
	{
		int icon_nr;

		if (status.charging)
		{
			if (*status.charge > 0.995)
				icon_nr = 5;
			else
				icon_nr = application::now() / 500'000'000 % 5;
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
	keyboard.display(hovered_item);

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
	for (const auto & [key, server]: application::get_config().servers)
	{
		imgui_ctx->add_chars(server.service.name);
	}

	imgui_ctx->new_frame(predicted_display_time);
	ImGuiStyle & style = ImGui::GetStyle();

	const float TabWidth = 300;

	auto last_hovered = hovered_item;
	hovered_item = 0;

	if (ImGui::GetIO().WantTextInput)
	{
		ImGui::SetNextWindowPos(imgui_ctx->layers()[2].vp_center(), ImGuiCond_Always, {0.5, 0.5});
		gui_keyboard();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 30);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 8, 8, 224));

	ImGui::SetNextWindowPos(imgui_ctx->layers()[0].vp_center(), ImGuiCond_Always, {0.5, 0.5});
	ImGui::SetNextWindowSize({1400, 900});

	ImGui::Begin("WiVRn", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	ImGui::SetCursorPos({TabWidth + 20, 0});

	{
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 20});
		ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0));
		ImGui::SetCursorPosY(20);

		switch (current_tab)
		{
			case tab::server_list:
				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {10, 10});
				draw_features_status(predicted_display_time);
				ImGui::PopStyleVar();

				gui_server_list();
				break;

			case tab::settings:
				gui_settings();
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

		ScrollWhenDraggingOnVoid();
		ImGui::EndChild();
		ImGui::PopStyleVar(2); // ImGuiStyleVar_FrameRounding, ImGuiStyleVar_WindowPadding
	}

	ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 255));
	ImGui::SetCursorPos(style.WindowPadding);
	{
		ImGui::BeginChild("Tabs", {TabWidth, ImGui::GetContentRegionMax().y - ImGui::GetWindowContentRegionMin().y});

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
		RadioButtonWithoutCheckBox(ICON_FA_COMPUTER "  " + _("Server list"), &current_tab, tab::server_list, {TabWidth, 0});
		vibrate_on_hover();

		RadioButtonWithoutCheckBox(ICON_FA_GEARS "  " + _("Settings"), &current_tab, tab::settings, {TabWidth, 0});
		vibrate_on_hover();

#if WIVRN_CLIENT_DEBUG_MENU
		RadioButtonWithoutCheckBox(ICON_FA_BUG_SLASH "  " + _("Debug"), &current_tab, tab::debug, {TabWidth, 0});
		vibrate_on_hover();
#endif

		ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - 3 * ImGui::GetCurrentContext()->FontSize - 6 * style.FramePadding.y - 2 * style.ItemSpacing.y - style.WindowPadding.y);
		RadioButtonWithoutCheckBox(ICON_FA_CIRCLE_INFO "  " + _("About"), &current_tab, tab::about, {TabWidth, 0});
		vibrate_on_hover();

		RadioButtonWithoutCheckBox(ICON_FA_SCALE_BALANCED "  " + _("Licenses"), &current_tab, tab::licenses, {TabWidth, 0});
		vibrate_on_hover();

		RadioButtonWithoutCheckBox(ICON_FA_DOOR_OPEN "  " + _("Exit"), &current_tab, tab::exit, {TabWidth, 0});
		vibrate_on_hover();

		ImGui::PopStyleVar(); // ImGuiStyleVar_FramePadding
		ImGui::EndChild();
	}
	ImGui::PopStyleColor(); // ImGuiCol_ChildBg
	ImGui::End();
	ImGui::PopStyleColor(); // ImGuiCol_WindowBg
	ImGui::PopStyleVar(2);  // ImGuiStyleVar_WindowPadding, ImGuiStyleVar_ScrollbarSize

	if (not is_gui_visible(*imgui_ctx, predicted_display_time))
	{
		if (application::get_hand_tracking_supported())
			display_recentering_tip(*imgui_ctx, _("Press A or X or put your palm up\nto move the main window"));
		else
			display_recentering_tip(*imgui_ctx, _("Press A or X to move the main window"));
	}

	if (hovered_item != last_hovered && hovered_item != 0)
	{
		size_t controller = imgui_ctx->get_focused_controller();
		if (controller < haptic_output.size())
			application::haptic_start(haptic_output[controller], XR_NULL_PATH, 10'000'000, 1000, 1);
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
