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
#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"
#include "lobby.h"
#include "stream.h"
#include "version.h"
#include <spdlog/fmt/fmt.h>
#include <utils/strings.h>

#ifdef __ANDROID__
#include "jnipp.h"
#endif

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

void scenes::lobby::vibrate_on_hover()
{
	if (ImGui::IsItemHovered())
		hovered_item = ImGui::GetItemID();
}

void scenes::lobby::gui_connecting()
{
	const ImVec2 button_size(220, 80);

	std::string status;
	if (next_scene)
	{
		if (next_scene->current_state() == scenes::stream::state::stalled)
			status = _("Video stream interrupted");
		else
			status = _("Waiting for video stream");
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

void scenes::lobby::gui_add_server()
{
	const ImVec2 button_size(220, 80);

	// TODO column widths
	ImGui::BeginTable("table", 2);

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::Text("%s", _S("Displayed name"));

	static char buf[100];
	ImGui::TableNextColumn();
	ImGui::InputText("##Name", buf, sizeof(buf));
	vibrate_on_hover();

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::Text("%s", _S("Host name"));

	static char buf2[100];
	ImGui::TableNextColumn();
	ImGui::InputText("##Hostname", buf2, sizeof(buf2));
	vibrate_on_hover();

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::Text("%s", _S("Port"));

	static int port;
	ImGui::TableNextColumn();
	ImGui::InputInt("##Port", &port);
	vibrate_on_hover();

	ImGui::EndTable();

	// TODO virtual keyboard
	// See https://github.com/qt/qtvirtualkeyboard/tree/dev/src/layouts/fallback
	//     https://github.com/qt/qtvirtualkeyboard/blob/dev/src/layouts/fr_FR/main.qml
	//     https://doc.qt.io/qt-6/qtvirtualkeyboard-layouts.html

	gui_keyboard(ImVec2(1000, 280));

	auto top_left = ImGui::GetWindowContentRegionMin();
	auto bottom_right = ImGui::GetWindowContentRegionMax();

	ImGui::SetCursorPosX(top_left.x);
	if (ImGui::Button("Cancel", button_size))
		current_tab = tab::server_list;
	vibrate_on_hover();

	ImGui::SameLine(bottom_right.x - button_size.x);

	if (ImGui::Button("Save", button_size))
	{
		current_tab = tab::server_list;
		configuration::server_data data{
		        .manual = true,
		        .service = {
		                .name = add_server_window_prettyname,
		                .hostname = add_server_window_hostname,
		                .port = add_server_window_port,
		        },
		};

		auto & config = application::get_config();
		config.servers.emplace("manual-" + data.service.name, data);
		config.save();
	}
	vibrate_on_hover();
}

void scenes::lobby::gui_server_list()
{
	auto & config = application::get_config();
	// Build an index of the cookies sorted by server name
	std::multimap<std::string, std::string> sorted_cookies;
	for (auto && [cookie, data]: config.servers)
	{
		sorted_cookies.emplace(data.service.name, cookie);
	}

	const ImVec2 button_size(220, 80);
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
		bool is_selected = (cookie == selected_item);

		ImGui::SetCursorPos(pos);

		ImGui::SetNextItemAllowOverlap();

		// TODO custom widget
		if (ImGui::Selectable(("##" + cookie).c_str(), is_selected, ImGuiSelectableFlags_None, ImVec2(0, list_item_height)))
			selected_item = cookie;

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

		// TODO
		// if (ImGui::IsItemHovered())
		// {
		// ImGui::SetTooltip("Tooltip");
		// }

		ImVec2 button_position(ImGui::GetWindowContentRegionMax().x - style.WindowPadding.x - 20, pos.y + (list_item_height - button_size.y) / 2);

		button_position.x -= button_size.x + style.WindowPadding.x;
		ImGui::SetCursorPos(button_position);

		bool enable_connect_button = (data.visible || data.manual) && data.compatible;
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
			if (!data.compatible)
				ImGui::SetTooltip("%s", _S("Incompatible server version"));
			else if (!data.visible && !data.manual)
				ImGui::SetTooltip("%s", _S("Server not available"));
		}

		ImGui::PopStyleColor(3);
		ImGui::EndDisabled();

		button_position.x -= button_size.x + style.WindowPadding.x;
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

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2);
	if (ImGui::BeginPopupModal("connecting", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
	{
		gui_connecting();
		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(3);
}

void scenes::lobby::gui_settings()
{
	auto & config = application::get_config();
	ImGuiStyle & style = ImGui::GetStyle();

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 20));

	{
		const auto & refresh_rates = session.get_refresh_rates();
		float current = session.get_current_refresh_rate();
		if (not refresh_rates.empty())
		{
			if (ImGui::BeginCombo(_S("Refresh rate"), fmt::format("{}", current).c_str()))
			{
				for (float rate: refresh_rates)
				{
					if (ImGui::Selectable(fmt::format("{}", rate).c_str(), rate == current) and rate != current)
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
				if (ImGui::Selectable(fmt::format(_F("{} - {}x{} per eye"), scale, (int)(width * scale), (int)(height * scale)).c_str(), scale == current) and scale != current)
				{
					config.resolution_scale = scale;
					config.save();
				}
			}
			ImGui::EndCombo();
		}
		vibrate_on_hover();
	}

	if (ImGui::Checkbox(_S("Enable microphone"), &config.microphone))
	{
#ifdef __ANDROID__
		if (config.microphone)
		{
			jni::object<""> act(application::native_app()->activity->clazz);
			auto app = act.call<jni::object<"android/app/Application">>("getApplication");
			auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

			jni::string permission("android.permission.RECORD_AUDIO");
			auto result = ctx.call<jni::Int>("checkSelfPermission", permission);
			if (result != 0 /*PERMISSION_GRANTED*/)
			{
				spdlog::info("RECORD_AUDIO permission not granted, requesting it");
				jni::array permissions(permission);
				act.call<void>("requestPermissions", permissions, jni::Int(0));
			}
			else
			{
				spdlog::info("RECORD_AUDIO permission already granted");
			}
		}
#endif
		config.save();
	}
	vibrate_on_hover();

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
		ImGui::SetTooltip("%s", _S("Overlay can be toggled by pressing both thumbsticks"));

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

void scenes::lobby::gui_about()
{
	ImGui::PushFont(imgui_ctx->large_font);
	CenterTextH(std::string("WiVRn ") + xrt::drivers::wivrn::git_version);
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

	const auto components = {"WiVRn", "openxr-loader", "simdjson"};
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
	if (ImGui::BeginCombo("", selected_item.c_str()))
	{
		for (const auto & component: components)
		{
			try
			{
				auto current = std::make_unique<asset>(std::filesystem::path("licenses") / component);
				if (ImGui::Selectable(component, component == selected_item))
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

static void ScrollWhenDraggingOnVoid(ImVec2 delta)
{
	// https://github.com/ocornut/imgui/issues/3379#issuecomment-1678718752
	ImGuiContext & g = *ImGui::GetCurrentContext();
	ImGuiWindow * window = g.CurrentWindow;
	bool hovered = false;
	bool held = false;
	ImGuiID id = window->GetID("##scrolldraggingoverlay");
	ImGui::KeepAliveID(id);
	if (g.HoveredId == 0) // If nothing hovered so far in the frame (not same as IsAnyItemHovered()!)
		ImGui::ButtonBehavior(window->Rect(), id, &hovered, &held, ImGuiButtonFlags_MouseButtonLeft);
	if (held && delta.x != 0.0f)
		ImGui::SetScrollX(window, window->Scroll.x + delta.x);
	if (held && delta.y != 0.0f)
		ImGui::SetScrollY(window, window->Scroll.y + delta.y);
}

XrCompositionLayerQuad scenes::lobby::draw_gui(XrTime predicted_display_time)
{
	for(const auto& [key, server]: application::get_config().servers)
	{
		imgui_ctx->add_chars(server.service.name);
	}

	imgui_ctx->new_frame(predicted_display_time);
	ImGuiStyle & style = ImGui::GetStyle();

	const float TabWidth = 300;

	auto last_hovered = hovered_item;
	hovered_item = 0;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 30);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 8, 8, 224));

	ImGui::SetNextWindowPos({50, 50});
	ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size - ImVec2(100, 100));
	// ImGui::SetNextWindowPos({0, 0});
	// ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);

	ImGui::Begin("WiVRn", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	ImGui::SetCursorPos({TabWidth + 20, 0});

	{
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
		ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX(), 0));
		ImGui::SetCursorPosY(20);

		switch (current_tab)
		{
			case tab::server_list:
				gui_server_list();
				break;

			case tab::new_server:
				break;

			case tab::settings:
				gui_settings();
				break;

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

		ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
		ScrollWhenDraggingOnVoid(ImVec2(0.0f, -mouse_delta.y));
		ImGui::EndChild();
		ImGui::PopStyleVar(); // ImGuiStyleVar_FrameRounding
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

	if (hovered_item != last_hovered && hovered_item != 0)
	{
		size_t controller = imgui_ctx->get_focused_controller();
		if (controller < haptic_output.size())
			application::haptic_start(haptic_output[controller], XR_NULL_PATH, 10'000'000, 1000, 1);
	}

	return imgui_ctx->end_frame();
}
