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

#include "lobby.h"
#include "wivrn_packets.h"
#include "../common/version.h"
#include <utils/strings.h>


static void CenterTextH(const std::string& text)
{
	float win_width = ImGui::GetWindowSize().x;
	float text_width = ImGui::CalcTextSize(text.c_str()).x;
	ImGui::SetCursorPosX((win_width - text_width) / 2);

	ImGui::Text("%s", text.c_str());
}

static void CenterTextHV(const std::string& text)
{
	ImVec2 size = ImGui::GetWindowSize();

	std::vector<std::string> lines = utils::split(text);

	float text_height = 0;
	for(const auto& i: lines)
		text_height += ImGui::CalcTextSize(i.c_str()).y;

	ImGui::SetCursorPosY((size.y - text_height) / 2);

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
	for(const auto& i: lines)
	{
		float text_width = ImGui::CalcTextSize(i.c_str()).x;
		ImGui::SetCursorPosX((size.x - text_width) / 2);
		ImGui::Text("%s", i.c_str());
	}
	ImGui::PopStyleVar();
}

void scenes::lobby::gui_connecting()
{
	const ImVec2 button_size(220, 80);

	std::string status;
	if (next_scene)
		status = "Waiting for video stream";
	else if (async_session.valid())
		status = async_session.get_progress();
	else if (async_error)
		status = *async_error;

	std::string button_label;

	if (async_error)
		button_label = "Close";
	else
		button_label = "Cancel";

	ImGui::Dummy({1000, 1});


	ImGui::PushFont(imgui_ctx->large_font);
	CenterTextH(fmt::format("Connection to {}", server_name));
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
	if (ImGui::IsItemHovered())
		hovered_item = "close";
}

void scenes::lobby::gui_add_server()
{
	const ImVec2 button_size(220, 80);

	// CenterNextWindow({1200, 900});

	ImGui::PushFont(imgui_ctx->large_font);
	CenterTextH("Add server");
	ImGui::PopFont();

	// TODO column widths
	ImGui::BeginTable("table", 2);

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::Text("Displayed name");

	static char buf[100];
	ImGui::TableNextColumn();
	ImGui::InputText("##Name", buf, sizeof(buf));
	if (ImGui::IsItemHovered())
		hovered_item = "name";

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::Text("Host name");

	static char buf2[100];
	ImGui::TableNextColumn();
	ImGui::InputText("##Hostname", buf2, sizeof(buf2));
	if (ImGui::IsItemHovered())
		hovered_item = "hostname";

	ImGui::TableNextRow();
	ImGui::TableNextColumn();
	ImGui::Text("Port");

	static int port;
	ImGui::TableNextColumn();
	ImGui::InputInt("##Port", &port);
	if (ImGui::IsItemHovered())
		hovered_item = "port";

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
		ImGui::CloseCurrentPopup();
	if (ImGui::IsItemHovered())
		hovered_item = "cancel";

	ImGui::SameLine(bottom_right.x - button_size.x);

	if (ImGui::Button("Save", button_size))
	{
		ImGui::CloseCurrentPopup();

		server_data data
		{
			.manual = true,
			.service = {
				.name = add_server_window_prettyname,
				.hostname = add_server_window_hostname,
				.port = add_server_window_port,
			},
		};

		servers.emplace("manual-" + data.service.name, data);
		save_config();

	}
	if (ImGui::IsItemHovered())
		hovered_item = "save";
}

void scenes::lobby::gui_server_list()
{
	// Build an index of the cookies sorted by server name
	std::multimap<std::string, std::string> sorted_cookies;
	for(auto&& [cookie, data]: servers)
	{
		sorted_cookies.emplace(data.service.name, cookie);
	}

	const ImVec2 button_size(220, 80);
	const float list_item_height = 100;
	auto& style = ImGui::GetStyle();

	ImGui::SetNextWindowPos({0, 0});
	ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
	ImGui::Begin("WiVRn", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove);

	ImGui::PushFont(imgui_ctx->large_font);
	CenterTextH(std::string("WiVRn ") + xrt::drivers::wivrn::git_version);
	ImGui::PopFont();

	std::string cookie_to_remove;

	float list_box_height = ImGui::GetWindowContentRegionMax().y - button_size.y - style.WindowPadding.y - ImGui::GetCursorPosY();

	if (ImGui::BeginListBox("##detected servers", ImVec2(-FLT_MIN, list_box_height)))
	{
		if (sorted_cookies.empty())
		{
			ImGui::PushFont(imgui_ctx->large_font);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.5));
			CenterTextHV("Start a WiVRn server on your\nlocal network");
			ImGui::PopStyleColor();
			ImGui::PopFont();
		}

		auto pos = ImGui::GetCursorPos();

		ImGui::PushStyleColor(ImGuiCol_Header, 0);
		ImGui::PushStyleColor(ImGuiCol_HeaderHovered, 0);
		ImGui::PushStyleColor(ImGuiCol_HeaderActive, 0);
		for(const auto& [name, cookie]: sorted_cookies)
		{
			server_data& data = servers.at(cookie);
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
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0,1,0,1));
				if (ImGui::Checkbox(("Autoconnect##" + cookie).c_str(), &data.autoconnect))
					save_config();
				if (ImGui::IsItemHovered())
					hovered_item = "autoconnect " + cookie;
				ImGui::PopStyleColor();
			}

			// TODO
			// if (ImGui::IsItemHovered())
			// {
				// ImGui::SetTooltip("Tooltip");
			// }

			ImVec2 button_position(ImGui::GetWindowContentRegionMax().x + style.FramePadding.x, pos.y + (list_item_height - button_size.y) / 2);

			button_position.x -= button_size.x + style.WindowPadding.x;
			ImGui::SetCursorPos(button_position);

			bool enable_connect_button = data.visible || data.manual;
			ImGui::BeginDisabled(!enable_connect_button);
			if (enable_connect_button)
			{
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.8f, 0.3f, 0.40f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.8f, 0.3f, 1.00f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.1f, 1.0f, 0.2f, 1.00f));
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.2f, 0.4f, 0.3f, 0.40f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.4f, 0.3f, 1.00f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.1f, 0.5f, 0.2f, 1.00f));
			}
			if (ImGui::Button(("Connect##" + cookie).c_str(), button_size))
			{
				connect(data);
				ImGui::OpenPopup("connecting");
			}

			if (ImGui::IsItemHovered())
				hovered_item = "connect " + cookie;

			ImGui::PopStyleColor(3);
			ImGui::EndDisabled();

			button_position.x -= button_size.x + style.WindowPadding.x;
			if (data.manual)
			{
				ImGui::SetCursorPos(button_position);
				ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 0.1f, 0.1f, 1.00f));

				if (ImGui::Button(("Remove##" + cookie).c_str(), button_size))
					cookie_to_remove = cookie;

				if (ImGui::IsItemHovered())
					hovered_item = "remove " + cookie;

				ImGui::PopStyleColor(3);
			}

			pos.y += 120;
		}
		ImGui::PopStyleColor(3);

		if (cookie_to_remove != "")
		{
			servers.erase(cookie_to_remove);
			save_config();
		}

		ImGui::EndListBox();
	}

	auto top_left = ImGui::GetWindowContentRegionMin();
	auto bottom_right = ImGui::GetWindowContentRegionMax();

#ifndef NDEBUG
	ImGui::SetCursorPos(ImVec2(top_left.x, bottom_right.y - button_size.y));
	if (ImGui::Button("Add server", button_size))
	{
		ImGui::OpenPopup("add_server");
		strcpy(add_server_window_prettyname, "");
		strcpy(add_server_window_hostname, "");
		add_server_window_port = xrt::drivers::wivrn::default_port;
	}

	if (ImGui::IsItemHovered())
	{
		hovered_item = "add";
		ImGui::SetTooltip("TODO");
	}
#endif

	ImGui::SetCursorPos(ImVec2(bottom_right.x - button_size.x, bottom_right.y - button_size.y));
	if (ImGui::Button("Exit", button_size))
		application::pop_scene();

	if (ImGui::IsItemHovered())
		hovered_item = "exit";

	if (ImGui::BeginPopupModal("add_server", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove))
	{
		gui_add_server();
		ImGui::EndPopup();
	}

	// Check if an automatic connection has started
	if ((async_session.valid() || next_scene) && !ImGui::IsPopupOpen("connecting"))
		ImGui::OpenPopup("connecting");

	if (ImGui::BeginPopupModal("connecting", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove))
	{
		gui_connecting();
		ImGui::EndPopup();
	}

	ImGui::End();
}

void scenes::lobby::draw_gui(XrTime predicted_display_time)
{
	int image_index = swapchain_imgui.acquire();
	swapchain_imgui.wait();

	std::optional<std::pair<glm::vec3, glm::quat>> head_position = application::locate_controller(application::view(), world_space, predicted_display_time);

	if (head_position)
	{
		move_gui(head_position->first, head_position->second, predicted_display_time);
	}

	imgui_ctx->new_frame(predicted_display_time);
	// ImGui::ShowDemoWindow();

	auto last_hovered = hovered_item;
	hovered_item = "";

	gui_server_list();

	if (hovered_item != last_hovered && hovered_item != "")
	{
		size_t controller = imgui_ctx->get_focused_controller();
		if (controller < haptic_output.size())
			application::haptic_start(haptic_output[controller], XR_NULL_PATH, 10'000'000, 1000, 1);
	}

	// Render the GUI to the imgui material
	// imgui_material->base_color_texture->image_view = imgui_ctx->render();
	// imgui_material->ds_dirty = true;

	imgui_ctx->render(swapchain_imgui.images()[image_index].image);
}
