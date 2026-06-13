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
#include "configuration.h"
#include "constants.h"
#include "gui_common.h"
#include "imgui.h"
#include "lobby.h"
#include "render/ui_theme.h"
#include "render/ui_widgets.h"
#include "scenes/stream.h" // IWYU pragma: keep
#include "utils/async.h"
#include "utils/i18n.h"
#include "utils/mapped_file.h"
#include "utils/overloaded.h"
#if WIVRN_CLIENT_DEBUG_MENU
#include "utils/ranges.h"
#endif
#include "version.h"
#include "xr/body_tracker.h"
#include <algorithm>
#include <cassert>
#include <chrono> // IWYU pragma: keep
#include <entt/entity/fwd.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/types.hpp>
#include <filesystem>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imspinner.h>
#include <magic_enum.hpp>
#include <memory>
#include <ranges>
#include <spdlog/fmt/fmt.h>
#include <string>
#include <uni_algo/case.h>
#include <utility>
#include <utils/strings.h>
#include <vulkan/vulkan_to_string.hpp>

#include "IconsFontAwesome6.h"

using namespace std::chrono_literals;

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

void scenes::lobby::gui_connecting(locked_notifiable<pin_request_data> & pin_request)
{
	using constants::style::button_size;

	std::string close_button_label = _("Disconnect");

	std::string status;
	if (next_scene)
	{
		current_tab = tab::connected;
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
	assert(next_scene);

	if (next_scene->apps.draw_gui(*imgui_ctx, _("Disconnect")) == app_launcher::Cancel)
	{
		next_scene->exit();
		current_tab = tab::server_list;
	}
}

void scenes::lobby::gui_disconnected()
{
	using constants::style::button_size;
	std::string close_button_label = _("Close");

	if (!async_error)
	{
		async_error = next_scene->pop_stream_error();

		if (!async_error)
		{
			next_scene.reset();
			ImGui::CloseCurrentPopup();
			return;
		}
	}

	ImGui::Dummy({1000, 1});

	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	if (server_name == "")
		CenterTextH(fmt::format(_F("Disconnected")));
	else
		CenterTextH(fmt::format(_F("Disconnected from {}"), server_name));
	ImGui::PopFont();

	ImGui::Text("%s", async_error->c_str());
	ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - button_size.x - ImGui::GetStyle().WindowPadding.x);

	if (ImGui::Button(close_button_label.c_str(), button_size))
	{
		async_error.reset();
	}
	imgui_ctx->vibrate_on_hover();
}

void scenes::lobby::gui_new_server()
{
	namespace ui = wivrn::ui;

	auto reset = [&] {
		add_server_window_prettyname = "";
		add_server_window_hostname = "";
		add_server_window_port = wivrn::default_port;
		add_server_tcp_only = false;
		add_server_cookie = "";
	};

	const float w = ImGui::GetContentRegionAvail().x;

	ImGui::TextUnformatted(_S("Name"));
	if (ImGui::IsWindowAppearing())
		ImGui::SetKeyboardFocusHere();
	ui::input_text("##name", add_server_window_prettyname, _S("My gaming PC"), w);
	ImGui::Dummy({0, 6});

	ImGui::TextUnformatted(_S("Address"));
	ui::input_text("##hostname", add_server_window_hostname, _S("192.168.1.10 or host.local"), w);
	ImGui::Dummy({0, 6});

	ImGui::TextUnformatted(_S("Port"));
	ui::input_int("##port", &add_server_window_port, 1, 1, 65535, w);
	ImGui::Dummy({0, 6});

	ImGui::TextUnformatted(_S("TCP only"));
	ImGui::SameLine();
	ui::toggle("##tcp", &add_server_tcp_only);

	ImGui::Dummy({0, 12});

	const float gap = ImGui::GetStyle().ItemSpacing.x;
	const std::string cancel_l = _("Cancel");
	const std::string save_l = _("Save");
	const float cancel_w = ImGui::CalcTextSize(cancel_l.c_str()).x + ui::metrics::button_padding.x * 2;
	const float save_w = ImGui::CalcTextSize(save_l.c_str()).x + ui::metrics::button_padding.x * 2;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - cancel_w - save_w - gap);

	if (ui::button(cancel_l, ui::button_style::secondary, {cancel_w, 0}))
	{
		reset();
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(add_server_window_hostname.empty());
	if (ui::button(save_l, ui::button_style::primary, {save_w, 0}))
	{
		configuration::server_data data{
		        .manual = true,
		        .service = {
		                .name = add_server_window_prettyname.empty() ? add_server_window_hostname : add_server_window_prettyname,
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

		reset();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
}

void scenes::lobby::gui_server_list()
{
	namespace ui = wivrn::ui;
	auto & config = application::get_config();
	const auto & t = ui::current();

	// header with an Add server button on the right
	const ImVec2 hstart = ImGui::GetCursorPos();
	const float header_avail = ImGui::GetContentRegionAvail().x;
	ui::page_header(_S("Computers"), _S("Pick a PC running the WiVRn server to stream from."));
	const ImVec2 hend = ImGui::GetCursorPos();

	const std::string add_label = std::string(ICON_FA_PLUS "  ") + _("Add server");
	const float add_w = ImGui::CalcTextSize(add_label.c_str()).x + ui::metrics::button_padding.x * 2;
	ImGui::SetCursorPos({hstart.x + header_avail - add_w, hstart.y});
	if (ui::button(add_label, ui::button_style::primary))
	{
		add_server_cookie = "";
		add_server_window_prettyname = "";
		add_server_window_hostname = "";
		add_server_window_port = wivrn::default_port;
		add_server_tcp_only = false;
		ImGui::OpenPopup("add or edit server");
	}
	ImGui::SetCursorPos(hend);

	std::multimap<std::string, std::string> sorted_cookies;
	for (auto && [cookie, data]: config.servers)
		sorted_cookies.emplace(data.service.name, cookie);

	std::string cookie_to_remove;
	// deferred: the action menu runs inside the per-row id stack, but the popup is
	// opened below in this scope so OpenPopup and begin_modal hash to the same id
	bool open_add_edit = false;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 10});
	ui::begin_card("##servers");
	{
		if (sorted_cookies.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, t.text_muted);
			ImGui::TextUnformatted(_S("Start a WiVRn server on your local network."));
			ImGui::PopStyleColor();
		}

		bool first = true;
		for (const auto & [name, cookie]: sorted_cookies)
		{
			configuration::server_data & data = config.servers.at(cookie);
			ImGui::PushID(cookie.c_str());
			if (not first)
				ui::row_separator();
			first = false;

			const bool available = (data.visible and data.compatible) or data.manual;
			const float gap = ImGui::GetStyle().ItemSpacing.x;
			const float bh = ImGui::GetFrameHeight() * ui::metrics::control_height;
			const std::string sub = fmt::format("{} : {}", data.service.hostname, data.service.port);

			// trailing controls: [Auto chip] [Connect] [menu], measured up front so the
			// row body click area can exclude them
			const std::string c_label = available ? std::string(ICON_FA_PLAY "  ") + _("Connect") : _("Unavailable");
			const float cw = ImGui::CalcTextSize(c_label.c_str()).x + ui::metrics::button_padding.x * 2;
			const std::string chip_label = std::string(ICON_FA_BOLT "  ") + _("Auto");
			const ImVec2 chip_ts = ImGui::CalcTextSize(chip_label.c_str());
			const float chip_w = chip_ts.x + ui::metrics::chip_padding.x * 2;
			float trailing = bh + gap + cw + (data.autoconnect ? gap + chip_w : 0) + ui::metrics::list_row_pad;

			const auto row = ui::begin_list_row("##row", ICON_FA_SERVER, 0, name, sub, false, trailing);
			const float row_h = row.max.y - row.min.y;
			float x = row.max.x;

			// overflow menu
			const std::string auto_l = _("Autoconnect");
			const std::string edit_l = _("Edit server");
			const std::string del_l = _("Delete server");
			std::vector<ui::action_item> actions;
			actions.push_back({ICON_FA_BOLT, auto_l.c_str(), false, data.autoconnect});
			if (data.manual)
			{
				actions.push_back({ICON_FA_PEN, edit_l.c_str(), false, false});
				actions.push_back({ICON_FA_TRASH, del_l.c_str(), true, false});
			}
			ImGui::SetCursorScreenPos({x - bh, row.min.y + (row_h - bh) * 0.5f});
			switch (ui::action_menu("##menu", ICON_FA_ELLIPSIS_VERTICAL, actions))
			{
				case 0:
					data.autoconnect = not data.autoconnect;
					config.save();
					break;
				case 1:
					add_server_cookie = cookie;
					add_server_window_prettyname = data.service.name;
					add_server_window_hostname = data.service.hostname;
					add_server_window_port = data.service.port;
					add_server_tcp_only = data.service.tcp_only;
					open_add_edit = true;
					break;
				case 2:
					cookie_to_remove = cookie;
					break;
			}
			x -= bh + gap;

			// connect / unavailable
			ImGui::SetCursorScreenPos({x - cw, row.min.y + (row_h - bh) * 0.5f});
			ImGui::BeginDisabled(not available);
			if (ui::button(c_label, available ? ui::button_style::primary : ui::button_style::secondary, {cw, 0}))
			{
				connect(data);
				ImGui::OpenPopup("connecting");
			}
			ImGui::EndDisabled();
			x -= cw + gap;

			// Auto chip
			if (data.autoconnect)
			{
				const float chip_h = chip_ts.y + ui::metrics::chip_padding.y * 2;
				ImGui::SetCursorScreenPos({x - chip_w, row.min.y + (row_h - chip_h) * 0.5f});
				ui::chip(chip_label, ui::chip_style::accent);
			}

			ui::end_list_row();
			ImGui::PopID();
		}

		ui::end_card();
	}
	ImGui::PopStyleVar();

	if (cookie_to_remove != "")
	{
		config.servers.erase(cookie_to_remove);
		config.save();
	}

	if (open_add_edit)
		ImGui::OpenPopup("add or edit server");

	// Check if we need to report errors after a disconnect
	if (next_scene and next_scene->current_state() == scenes::stream::state::shutdown and not ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
		ImGui::OpenPopup("disconnected");

	// Check if an automatic connection has started
	if ((async_session.valid() || next_scene) and not ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
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

	ImGui::SetNextWindowPos({popup_layer_center.x, popup_layer_center.y}, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("disconnected", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		gui_disconnected();
		ImGui::EndPopup();
	}

	if (wivrn::ui::begin_modal("add or edit server", add_server_cookie.empty() ? _("Add server") : _("Edit server"), 620))
	{
		gui_new_server();
		wivrn::ui::end_modal();
	}
	ImGui::PopStyleVar(3);
}

#if WIVRN_CLIENT_DEBUG_MENU
void scenes::lobby::gui_debug_node_hierarchy(entt::entity root)
{
	for (auto && [entity, node]: world.view<components::node>().each())
	{
		if (node.parent != root)
			continue;

		std::string id = "entity-" + std::to_string((int)entity);
		const std::string & name = node.name == "" ? id : node.name;

		// See ImGui::StyleColorsDark
		if (node.global_visible)
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
		else
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.50f, 0.50f, 1.00f));

		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		if (ImGui::TreeNodeEx(id.c_str(), ImGuiTreeNodeFlags_None, "%s", name.c_str()))
		{
			glm::vec3 scale{
			        glm::length(glm::column(node.transform_to_root, 0)),
			        glm::length(glm::column(node.transform_to_root, 1)),
			        glm::length(glm::column(node.transform_to_root, 2)),
			};

			gui_debug_node_hierarchy(entity);

			if (node.mesh)
				for (auto [index, primitive]: utils::enumerate(node.mesh->primitives))
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					std::string name = "Primitive " + std::to_string(index + 1);

					ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf;
					if (debug_primitive_to_highlight == std::pair{entity, index})
						flags |= ImGuiTreeNodeFlags_Selected;

					if (ImGui::TreeNodeEx(name.c_str(), flags))
					{
						if (ImGui::IsItemClicked())
							debug_primitive_to_highlight = {entity, index};

						if (ImGui::IsItemHovered())
						{
							std::string tooltip = fmt::format(
							        "Name: {}\n"
							        "Topology: {}\n"
							        "Vertices: {}\n"
							        "Material: {}\n"
							        "vertex shader: {}\n"
							        "Fragment shader: {}\n",
							        name,
							        vk::to_string(primitive.topology),
							        primitive.vertex_count,
							        primitive.material_->name,
							        primitive.vertex_shader,
							        primitive.material_->fragment_shader);

							imgui_ctx->tooltip(tooltip);
						}

						ImGui::TreePop();
					}

					ImGui::TableNextColumn();

					glm::vec3 size = scale * (primitive.obb_max - primitive.obb_min);
					ImGui::Text("%s", fmt::format("{} vertices, {:.3f} x {:.3f} x {:.3f} m", primitive.vertex_count, size.x, size.y, size.z).c_str());
				}

			ImGui::TreePop();
		}
		ImGui::TableNextRow();
		ImGui::PopStyleColor(); // ImGuiCol_Text
	}
}

void scenes::lobby::gui_debug()
{
	ImGui::GetIO().ConfigDragClickToInputText = true;

	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20, 20));

	if (ImGui::CollapsingHeader("Tune controller offset"))
	{
		imgui_ctx->vibrate_on_hover();
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
	}
	else
		imgui_ctx->vibrate_on_hover();

	if (ImGui::Button("Delete configuration file"))
		std::filesystem::remove(application::get_config_path() / "client.json");
	imgui_ctx->vibrate_on_hover();

	if (ImGui::CollapsingHeader("CPU / GPU stats"))
	{
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
	}
	else
		imgui_ctx->vibrate_on_hover();

	if (ImGui::CollapsingHeader("Renderer stats"))
	{
		imgui_ctx->vibrate_on_hover();

		const auto & stats = renderer->last_frame_stats();

		ImGui::Text("Primitives: %zd total, %zd culled", stats.count_primitives, stats.count_culled_primitives);
		ImGui::Text("Triangles: %zd total, %zd culled", stats.count_triangles, stats.count_culled_triangles);

		if (ImGui::BeginTable("Node hierarchy", 2, ImGuiTableFlags_None))
		{
			gui_debug_node_hierarchy();
			ImGui::EndTable();
		}
	}
	else
		imgui_ctx->vibrate_on_hover();

	ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
}
#endif

void scenes::lobby::gui_about()
{
	ImGui::PushFont(nullptr, constants::gui::font_size_large);
	CenterTextH(std::string("WiVRn ") + wivrn::display_version());
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

	config.set_feature(feature::hand_tracking, true);

	while (optional_feature_index < optional_features.size() and
	       (not optional_features[optional_feature_index].supported or
	        config.check_feature(optional_features[optional_feature_index].f)))

		optional_feature_index++;

	if (optional_feature_index == optional_features.size())
	{
		current_tab = tab::server_list;
		config.first_run = false;
		config.save();
		ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
		return;
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
			license = std::make_unique<utils::mapped_file>(std::filesystem::path("assets://licenses") / selected_item);
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
				auto current = std::make_unique<utils::mapped_file>(std::filesystem::path("assets://licenses") / component);
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

void scenes::lobby::update_transfers()
{
	for (auto & [name, transfer]: current_transfers)
	{
		transfer.first.sync();
		if (transfer.first.get_state() == libcurl::state::done and transfer.second)
			transfer.second(transfer.first);
	}

	std::erase_if(current_transfers, [](auto & x) {
		auto state = x.second.first.get_state();
		return state == libcurl::state::done or state == libcurl::state::reset;
	});
}

void scenes::lobby::download(const std::string & url, const std::filesystem::path & path, std::function<void(libcurl::curl_handle & handle)> callback)
{
	current_transfers.emplace(url, std::make_pair(curl.download(url, path), std::move(callback)));
}

void scenes::lobby::download(const std::string & url, std::function<void(libcurl::curl_handle & handle)> callback)
{
	current_transfers.emplace(url, std::make_pair(curl.download(url), std::move(callback)));
}

libcurl::curl_handle * scenes::lobby::try_get_download_handle(const std::string & url)
{
	auto iter = current_transfers.find(url);
	if (iter == current_transfers.end())
		return nullptr;
	else
		return &iter->second.first;
}

std::vector<std::pair<int, XrCompositionLayerQuad>> scenes::lobby::draw_gui(XrTime predicted_display_time)
{
	imgui_ctx->new_frame(predicted_display_time);
	update_transfers();
	update_file_picker();

	ImGuiStyle & style = ImGui::GetStyle();
	style.FontScaleMain = wivrn::ui::current().font_scale * wivrn::ui::metrics::font_base;

	// base text colour follows the theme so plain ImGui text is legible on light themes
	style.Colors[ImGuiCol_Text] = wivrn::ui::current().text;
	style.Colors[ImGuiCol_TextDisabled] = wivrn::ui::current().text_muted;

	// combo modals open centred on the popup layer
	const auto & ui_popup_layer = imgui_ctx->layers()[1];
	const glm::vec2 ui_popup_center = ui_popup_layer.vp_origin + ui_popup_layer.vp_size / 2;
	wivrn::ui::set_popup_center({ui_popup_center.x, ui_popup_center.y});

	// let the themed widgets fire the hover haptic and show tooltips
	wivrn::ui::set_hover_haptic([this] { imgui_ctx->vibrate_on_hover(); });
	wivrn::ui::set_tooltip_hook([this](const char * text) { imgui_ctx->tooltip(text); });

	const float TabWidth = 300;

	if (ImGui::GetIO().WantTextInput)
	{
		ImGui::SetNextWindowPos(imgui_ctx->layers()[2].vp_center(), ImGuiCond_Always, {0.5, 0.5});
		gui_keyboard();
	}

	ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 30);
	// window background follows the theme, with a user-controlled opacity
	const wivrn::ui::theme & th = wivrn::ui::current();
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{th.background.x, th.background.y, th.background.z, th.background_alpha});

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

		auto & config = application::get_config();
		const float TopBarH = 72;

		// Top bar: logo on the left, status controls on the right
		ImGui::SetCursorPos({0, 0});
		ImGui::PushStyleColor(ImGuiCol_ChildBg, wivrn::ui::current().background);
		ImGui::BeginChild("TopBar", {ImGui::GetWindowSize().x, TopBarH}, 0, ImGuiWindowFlags_NoScrollbar);
		{
			const float side = ImGui::GetFrameHeight() * wivrn::ui::metrics::control_height;
			const float gap = 8;

			// logo
			ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
			ImGui::SetCursorPos({24, (TopBarH - ImGui::GetFontSize()) * 0.5f});
			ImGui::PushStyleColor(ImGuiCol_Text, wivrn::ui::current().accent);
			ImGui::TextUnformatted(ICON_FA_EYE);
			ImGui::PopStyleColor();
			ImGui::SameLine(0, 10);
			ImGui::TextUnformatted("WiVRn");
			ImGui::PopFont();

			// right cluster: feature toggles + battery + connection status
			const std::string conn = _("Not connected");
			const ImVec2 cts = ImGui::CalcTextSize(conn.c_str());
			const float dot_r = cts.y * 0.26f;
			const float chip_w = cts.x + wivrn::ui::metrics::chip_padding.x * 2.5f + dot_r * 2;

			// feature toggles available on this headset (mic + passthrough are always shown)
			const bool has_hand = system.hand_tracking_supported();
			const bool has_eye = application::get_eye_gaze_supported();
			const bool has_face = system.face_tracker_supported() != xr::face_tracker_type::none;
			const bool has_body = system.body_tracker_supported() != xr::body_tracker_type::none;
			const int n_toggles = 2 + has_hand + has_eye + has_face + has_body;

#ifdef __ANDROID__
			const auto battery = get_battery_status();
			std::string battery_label;
			wivrn::ui::chip_style battery_style = wivrn::ui::chip_style::neutral;
			if (battery.charge)
			{
				const char * battery_icon = ICON_FA_BATTERY_FULL;
				int icon_nr;
				if (battery.charging)
					icon_nr = *battery.charge > 0.995 ? 5 : instance.now() / 500'000'000 % 5;
				else
					icon_nr = std::round((*battery.charge) * 4);
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
						break;
				}
				battery_label = fmt::format("{} {}%", battery_icon, (int)std::round(*battery.charge * 100));
				if (*battery.charge < 0.2)
					battery_style = wivrn::ui::chip_style::danger;
			}
			const float battery_w = battery_label.empty() ? 0 : gap + ImGui::CalcTextSize(battery_label.c_str()).x + wivrn::ui::metrics::chip_padding.x * 2;
#else
			const float battery_w = 0;
#endif
			const float cluster_w = n_toggles * side + n_toggles * gap + battery_w + chip_w;
			ImGui::SetCursorPos({ImGui::GetWindowSize().x - 24 - cluster_w, (TopBarH - side) * 0.5f});

			bool mic = config.check_feature(feature::microphone);
			if (wivrn::ui::icon_button(ICON_FA_MICROPHONE, {side, side}, mic, _S("Microphone")))
				config.set_feature(feature::microphone, not mic);
			ImGui::SameLine(0, gap);
			if (wivrn::ui::icon_button(ICON_FA_EYE_SLASH, {side, side}, config.passthrough_enabled, _S("Passthrough")))
			{
				config.passthrough_enabled = not config.passthrough_enabled;
				setup_passthrough();
				config.save();
			}
			if (has_hand)
			{
				ImGui::SameLine(0, gap);
				bool hand = config.check_feature(feature::hand_tracking);
				if (wivrn::ui::icon_button(ICON_FA_HAND, {side, side}, hand, _S("Hand tracking")))
					config.set_feature(feature::hand_tracking, not hand);
			}
			if (has_eye)
			{
				ImGui::SameLine(0, gap);
				bool eye = config.check_feature(feature::eye_gaze);
				if (wivrn::ui::icon_button(ICON_FA_EYE, {side, side}, eye, _S("Eye tracking")))
					config.set_feature(feature::eye_gaze, not eye);
			}
			if (has_face)
			{
				ImGui::SameLine(0, gap);
				bool face = config.check_feature(feature::face_tracking);
				if (wivrn::ui::icon_button(ICON_FA_FACE_SMILE, {side, side}, face, _S("Face tracking")))
					config.set_feature(feature::face_tracking, not face);
			}
			if (has_body)
			{
				ImGui::SameLine(0, gap);
				bool body = config.check_feature(feature::body_tracking);
				if (wivrn::ui::icon_button(ICON_FA_PERSON, {side, side}, body, _S("Body tracking")))
					config.set_feature(feature::body_tracking, not body);
			}

#ifdef __ANDROID__
			// battery status, as a pill like the connection chip
			if (not battery_label.empty())
			{
				ImGui::SameLine(0, gap);
				ImGui::SetCursorPosY((TopBarH - side) * 0.5f);
				wivrn::ui::chip(battery_label, battery_style, false, side);
			}
#endif

			ImGui::SameLine(0, gap);
			ImGui::SetCursorPosY((TopBarH - side) * 0.5f);
			wivrn::ui::chip(conn.c_str(), wivrn::ui::chip_style::muted, true, side);
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();

		// content area, with an equal margin on both sides of the panel
		const float content_margin = 20;
		ImGui::SetCursorPos({TabWidth + content_margin, TopBarH});

		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20, 20});
		ImGui::BeginChild("Main", ImVec2(ImGui::GetWindowSize().x - ImGui::GetCursorPosX() - content_margin, 0), 0);
		ImGui::SetCursorPosY(20);

		switch (current_tab)
		{
			case tab::first_run:
			case tab::connected:
				__builtin_unreachable();

			case tab::server_list:
				gui_server_list();
				break;

			case tab::performance:
				gui_performance();
				break;

			case tab::streaming:
				gui_streaming();
				break;

			case tab::audio:
				gui_audio();
				break;

			case tab::devices:
				gui_devices();
				break;

			case tab::tracking:
				gui_tracking();
				break;

			case tab::system:
				gui_system();
				break;

			case tab::post_processing:
				gui_post_processing();
				break;

			case tab::customize:
				gui_customize(predicted_display_time);
				break;

#if WIVRN_CLIENT_DEBUG_MENU
			case tab::components:
				gui_components();
				break;

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

		ImGui::PushStyleColor(ImGuiCol_ChildBg, wivrn::ui::current().card);
		ImGui::SetCursorPos({0, TopBarH});
		{
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 16});
			ImGui::BeginChild("Tabs", {TabWidth, ImGui::GetWindowSize().y - TopBarH}, ImGuiChildFlags_AlwaysUseWindowPadding);

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 4});

			if (wivrn::ui::nav_item(ICON_FA_COMPUTER, _S("Computers"), current_tab == tab::server_list))
				current_tab = tab::server_list;

			wivrn::ui::nav_section(_S("SETTINGS"));
			if (wivrn::ui::nav_item(ICON_FA_GAUGE_HIGH, _S("Performance"), current_tab == tab::performance))
				current_tab = tab::performance;
			if (wivrn::ui::nav_item(ICON_FA_TOWER_BROADCAST, _S("Streaming"), current_tab == tab::streaming))
				current_tab = tab::streaming;
			if (wivrn::ui::nav_item(ICON_FA_WAND_MAGIC_SPARKLES, _S("Post-processing"), current_tab == tab::post_processing))
				current_tab = tab::post_processing;
			if (wivrn::ui::nav_item(ICON_FA_VOLUME_HIGH, _S("Audio"), current_tab == tab::audio))
				current_tab = tab::audio;
			if (wivrn::ui::nav_item(ICON_FA_KEYBOARD, _S("Devices"), current_tab == tab::devices))
				current_tab = tab::devices;
			if (wivrn::ui::nav_item(ICON_FA_LOCATION_CROSSHAIRS, _S("Tracking"), current_tab == tab::tracking))
				current_tab = tab::tracking;
			if (wivrn::ui::nav_item(ICON_FA_GEARS, _S("System"), current_tab == tab::system))
				current_tab = tab::system;

			wivrn::ui::nav_section(_S("PERSONALIZE"));
			if (wivrn::ui::nav_item(ICON_FA_IMAGE, _S("Customize"), current_tab == tab::customize))
				current_tab = tab::customize;
#if WIVRN_CLIENT_DEBUG_MENU
			if (wivrn::ui::nav_item(ICON_FA_PALETTE, "Components", current_tab == tab::components))
				current_tab = tab::components;
			if (wivrn::ui::nav_item(ICON_FA_BUG_SLASH, _S("Debug"), current_tab == tab::debug))
				current_tab = tab::debug;
#endif

			// pinned to the bottom
			const float item_h = ImGui::GetFrameHeight() * wivrn::ui::metrics::control_height + ImGui::GetStyle().ItemSpacing.y;
			ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - 3 * item_h - ImGui::GetStyle().WindowPadding.y);
			if (wivrn::ui::nav_item(ICON_FA_CIRCLE_INFO, _S("About"), current_tab == tab::about))
				current_tab = tab::about;
			if (wivrn::ui::nav_item(ICON_FA_SCALE_BALANCED, _S("Licenses"), current_tab == tab::licenses))
				current_tab = tab::licenses;
			if (wivrn::ui::nav_item(ICON_FA_DOOR_OPEN, _S("Exit"), current_tab == tab::exit))
				current_tab = tab::exit;

			ImGui::PopStyleVar(); // ImGuiStyleVar_ItemSpacing
			ImGui::EndChild();
			ImGui::PopStyleVar(); // ImGuiStyleVar_WindowPadding (Tabs)
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
