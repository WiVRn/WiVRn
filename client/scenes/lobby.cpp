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
#include "../common/version.h"
#include "application.h"
#include "glm/geometric.hpp"
#include "imgui.h"
#include "input_profile.h"
#include "openxr/openxr.h"
#include "render/scene_data.h"
#include "stream.h"
#include "hardware.h"
#include <glm/gtc/matrix_access.hpp>

#include "wivrn_discover.h"
#include "wivrn_packets.h"
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <ios>
#include <linux/in.h>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <utils/ranges.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <simdjson.h>
#include <fstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>


static const std::string discover_service = "_wivrn._tcp.local.";

scenes::lobby::~lobby()
{
	if (renderer)
		renderer->wait_idle();
}

static std::string choose_webxr_profile()
{
#ifndef __ANDROID__
	const char * controller = std::getenv("WIVRN_CONTROLLER");
	if (controller && strcmp(controller, ""))
		return controller;
#endif

	switch(guess_model())
	{
		case model::oculus_quest:
			return "oculus-touch-v2";
		case model::oculus_quest_2:
			return "oculus-touch-v3";
		case model::meta_quest_pro:
			return "meta-quest-touch-pro";
		case model::meta_quest_3:
			return "meta-quest-touch-plus";
		case model::pico_neo_3:
			return "pico-neo3";
		case model::pico_4:
			return "pico-4";
		case model::htc_vive_focus_3:
		case model::htc_vive_xr_elite:
			return "htc-vive-focus-3";
		case model::unknown:
			return "generic-trigger-squeeze";
	}

	__builtin_unreachable();
}

static std::string json_string(const std::string& in)
{
	std::string out;
	out.reserve(in.size() + 2);

	out += '"';

	for(char c: in)
	{
		switch(c)
		{
			case '\b':
				out += "\\b";
				break;

			case '\f':
				out += "\\f";
				break;

			case '\n':
				out += "\\n";
				break;

			case '\r':
				out += "\\r";
				break;

			case '\t':
				out += "\\t";
				break;

			case '"':
				out += "\\\"";
				break;

			case '\\':
				out += "\\\\";
				break;

			default:
				out += c;
				break;
		}
	}

	out += '"';

	return out;
}

static const std::array supported_formats =
{
	vk::Format::eR8G8B8A8Srgb,
	vk::Format::eB8G8R8A8Srgb
};

void scenes::lobby::move_gui(glm::vec3 position, glm::quat orientation, XrTime predicted_display_time)
{
	const float gui_target_distance = 1.5;

	glm::vec3 gui_direction = glm::column(glm::mat3_cast(imgui_node->rotation), 2);
	float gui_yaw = atan2(gui_direction.x, gui_direction.z);

	glm::vec3 head_direction = -glm::column(glm::mat3_cast(orientation), 2);
	float head_yaw = atan2(head_direction.x, head_direction.z) + M_PI;

	head_direction.y = 0;
	head_direction = glm::normalize(head_direction);
	position.y = 1.5f;

	glm::vec3 gui_target_position = position + gui_target_distance * head_direction;

	glm::vec3 gui_position_error = gui_target_position - imgui_node->translation;
	float gui_yaw_error = remainderf(head_yaw - gui_yaw, 2 * M_PI);

	if (move_gui_first_time)
	{
		move_gui_first_time = false;
		gui_yaw += gui_yaw_error;
		imgui_node->translation += gui_position_error;
		imgui_node->rotation = glm::quat{ cos(gui_yaw/2), 0, sin(gui_yaw/2), 0 };
	}
	else
	{
		// gui_yaw += gui_yaw_error * std::min(1.0f, dt / tau);
		// imgui_node->translation += gui_position_error * std::min(1.0f, dt / tau);
	}

}

scenes::lobby::lobby()
{
	try
	{
		simdjson::dom::parser parser;
		simdjson::dom::element root = parser.load(application::get_config_path() / "client.json");
		for(simdjson::dom::object i: simdjson::dom::array(root["servers"]))
		{
			server_data data{
				.autoconnect = i["autoconnect"].get_bool(),
				.manual = i["manual"].get_bool(),
				.visible = false,
				.service = {
					.name = (std::string)i["pretty_name"],
					.hostname = (std::string)i["hostname"],
					.port = (int)i["port"].get_int64(),
					.txt = {{"cookie", (std::string)i["cookie"]}}

				}
			};
			servers.emplace(data.service.txt["cookie"], data);
		}
	}
	catch(std::exception& e)
	{
		spdlog::warn("Cannot read configuration: {}", e.what());
		servers.clear();
	}

	spdlog::info("{} known server(s):", servers.size());
	for(auto& i: servers)
	{
		spdlog::info("    {}", i.second.service.name);
	}

	haptic_output[0] = get_action("left_haptic").first;
	haptic_output[1] = get_action("right_haptic").first;

	swapchain_format = vk::Format::eUndefined;
	spdlog::info("Supported swapchain formats:");

	for (auto format: session.get_swapchain_formats())
	{
		spdlog::info("    {}", vk::to_string(format));
	}
	for (auto format: session.get_swapchain_formats())
	{
		if (std::find(supported_formats.begin(), supported_formats.end(), format) != supported_formats.end())
		{
			swapchain_format = format;
			break;
		}
	}

	if (swapchain_format == vk::Format::eUndefined)
		throw std::runtime_error("No supported swapchain format");

	spdlog::info("Using format {}", vk::to_string(swapchain_format));
}

void scenes::lobby::save_config()
{
	std::stringstream ss;

	for (auto& [cookie, server_data]: servers)
	{
		ss << "{";
		ss << "\"autoconnect\":" << std::boolalpha << server_data.autoconnect << ",";
		ss << "\"manual\":" << std::boolalpha << server_data.manual << ",";
		ss << "\"pretty_name\":" << json_string(server_data.service.name) << ",";
		ss << "\"hostname\":" << json_string(server_data.service.hostname) << ",";
		ss << "\"port\":" << server_data.service.port << ",";
		ss << "\"cookie\":" << json_string(cookie);
		ss << "},";
	}

	std::string servers_str = ss.str();
	if (servers_str != "")
		servers_str.pop_back(); // Remove last comma

	std::ofstream json(application::get_config_path() / "client.json");

	json << "{\"servers\":[" << servers_str << "]}";
}

static std::string ip_address_to_string(const in_addr& addr)
{
	char buf[100];
	inet_ntop(AF_INET, &addr, buf, sizeof(buf));
	return buf;
}

static std::string ip_address_to_string(const in6_addr& addr)
{
	char buf[100];
	inet_ntop(AF_INET6, &addr, buf, sizeof(buf));
	return buf;
}

std::unique_ptr<wivrn_session> connect_to_session(wivrn_discover::service service, bool manual_connection)
{
	// TODO: make it asynchronous

	if (!manual_connection)
	{
		char protocol_string[17];
		sprintf(protocol_string, "%016lx", xrt::drivers::wivrn::protocol_version);

		auto protocol = service.txt.find("protocol");
		if (protocol == service.txt.end())
			throw std::runtime_error("Incompatible WiVRn server: no protocol field in TXT");

		if (protocol->second != protocol_string)
			throw std::runtime_error(fmt::format("Incompatible WiVRn server protocol (client: {}, server: {})", protocol_string, protocol->second));
	}

	// Only the automatically discovered servers already have their IP addresses available
	if (manual_connection)
	{
		addrinfo hint
		{
			.ai_flags = AI_ADDRCONFIG,
			.ai_family = AF_UNSPEC,
			.ai_socktype = SOCK_STREAM,
		};
		addrinfo * addresses;
		if (int err = getaddrinfo(service.hostname.c_str(), nullptr, &hint, &addresses))
		{
			spdlog::error("Cannot get address for {}: {}", service.hostname, gai_strerror(err));
			return {};
		}

		for(auto i = addresses; i; i = i->ai_next)
		{
			switch (i->ai_family)
			{
				case AF_INET:
					service.addresses.push_back(((sockaddr_in*)i->ai_addr)->sin_addr);
					break;
				case AF_INET6:
					service.addresses.push_back(((sockaddr_in6*)i->ai_addr)->sin6_addr);
					break;
			}
		}

		freeaddrinfo(addresses);
	}

	for (const auto & address: service.addresses)
	{
		try
		{
			return std::visit([port = service.port](auto & address) {
				return std::make_unique<wivrn_session>(address, port);
			},
			address);
		}
		catch (std::exception & e)
		{
			std::string address_string = std::visit([](auto & address) {
				return ip_address_to_string(address);
			}, address);
			spdlog::warn("Cannot connect to {} ({}): {}", service.hostname, address_string, e.what());
		}
	}

	throw std::runtime_error("No usable address");
}

static glm::mat4 projection_matrix(XrFovf fov, float zn = 0.02)
{
	float r = tan(fov.angleRight);
	float l = tan(fov.angleLeft);
	float t = tan(fov.angleUp);
	float b = tan(fov.angleDown);

	// clang-format off
	return glm::mat4{
		{ 2/(r-l),     0,            0,    0 },
		{ 0,           2/(b-t),      0,    0 },
		{ (l+r)/(r-l), (t+b)/(b-t), -1,   -1 },
		{ 0,           0,           -2*zn, 0 }
	};
	// clang-format on
}

static glm::mat4 view_matrix(XrPosef pose)
{
	XrQuaternionf q = pose.orientation;
	XrVector3f pos = pose.position;

	glm::mat4 inv_view_matrix = glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z));

	inv_view_matrix = glm::translate(glm::mat4(1), glm::vec3(pos.x, pos.y, pos.z)) * inv_view_matrix;

	return glm::inverse(inv_view_matrix);
}

static void CenterText(const std::string& text)
{
	float win_width = ImGui::GetWindowSize().x;
	float text_width = ImGui::CalcTextSize(text.c_str()).x;
	ImGui::SetCursorPosX((win_width - text_width) / 2);

	ImGui::Text("%s", text.c_str());
}

static void CenterNextWindow(ImVec2 size)
{
	ImGui::SetNextWindowPos((ImGui::GetMainViewport()->Size - size) * 0.5f);
	ImGui::SetNextWindowSize(size);
}

void scenes::lobby::update_server_list()
{
	std::vector<wivrn_discover::service> discovered_services = discover->get_services();

	// TODO: only if discovered_services changed
	for(auto& [cookie, data]: servers)
	{
		data.visible = false;
	}

	for(auto& service: discovered_services)
	{
		std::string cookie;
		if (service.txt.find("cookie") == service.txt.end())
			cookie = service.hostname;
		else
			cookie = service.txt.at("cookie");

		auto server = servers.find(cookie);
		if (server == servers.end())
		{
			// Newly discovered server: add it to the list
			servers.emplace(cookie, server_data{
				.autoconnect = false,
				.manual = false,
				.visible = true,
				.service = service
			});
		}
		else
		{
			server->second.visible = true;
			server->second.service = service;
		}
	}
}

void scenes::lobby::connect(server_data& data, bool manual)
{
	try
	{
		if (auto session = connect_to_session(data.service, manual))
		{
			next_scene = stream::create(std::move(session));
			server_name = data.service.name;
		}
	}
	catch (const std::exception & e)
	{
		// TODO: dialog box
		spdlog::error("Failed to create stream session: {}", e.what());
	}
}

void scenes::lobby::gui_connecting()
{
	CenterNextWindow({1000, 500});
	ImGui::Begin("Connection", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove);

	ImGui::PushFont(imgui_ctx->large_font);
	CenterText("Waiting for video stream");
	ImGui::PopFont();

	CenterText(fmt::format("Connection to {}", server_name));

	ImGui::End();
}

void scenes::lobby::gui_server_list()
{
	// Build an index of the cookies sorted by server name
	std::multimap<std::string, std::string> sorted_cookies;
	for(auto&& [cookie, data]: servers)
	{
		sorted_cookies.emplace(data.service.name, cookie);

		if (data.visible && data.autoconnect && !next_scene)
			connect(data, false);
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
	CenterText(std::string("WiVRn ") + xrt::drivers::wivrn::git_version);
	ImGui::PopFont();

	std::string cookie_to_remove;

	float list_box_height = ImGui::GetWindowContentRegionMax().y - button_size.y - style.WindowPadding.y - ImGui::GetCursorPosY();

	if (ImGui::BeginListBox("##detected servers", ImVec2(-FLT_MIN, list_box_height)))
	{
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
				ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0,255,0,255));
				if (ImGui::Checkbox("Autoconnect", &data.autoconnect))
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
			if (ImGui::Button("Connect", button_size))
				connect(data, true);

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

				if (ImGui::Button("Remove", button_size))
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
		show_add_server_window = true;
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

	ImGui::End();
}

void scenes::lobby::gui_add_server()
{
	const ImVec2 button_size(220, 80);

	CenterNextWindow({1200, 900});
	ImGui::Begin("Add server", nullptr,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove);

	ImGui::PushFont(imgui_ctx->large_font);
	CenterText("Add server");
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

	// ImGui::SetCursorPos(ImVec2(top_left.x + 250, bottom_right.y - button_size.y));
	// ImGui::Text("ActiveId=%x", ImGui::GetCurrentContext()->ActiveId);

	ImGui::SetCursorPos(ImVec2(top_left.x, bottom_right.y - button_size.y));
	if (ImGui::Button("Cancel", button_size))
		show_add_server_window = false;
	if (ImGui::IsItemHovered())
		hovered_item = "cancel";

	ImGui::SetCursorPos(ImVec2(bottom_right.x - button_size.x, bottom_right.y - button_size.y));
	if (ImGui::Button("Save", button_size))
	{
		show_add_server_window = false;

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

	ImGui::End();
}

void scenes::lobby::draw_gui(XrTime predicted_display_time)
{
	std::optional<std::pair<glm::vec3, glm::quat>> head_position = application::locate_controller(application::view(), world_space, predicted_display_time);

	if (head_position)
	{
		move_gui(head_position->first, head_position->second, predicted_display_time);
	}

	imgui_ctx->set_position(imgui_node->translation, imgui_node->rotation);
	imgui_ctx->new_frame(predicted_display_time);
	// ImGui::ShowDemoWindow();

	auto last_hovered = hovered_item;
	hovered_item = "";

	if (next_scene)
		gui_connecting();
	else if (show_add_server_window)
		gui_add_server();
	else
		gui_server_list();


	if (hovered_item != last_hovered && hovered_item != "")
	{
		size_t controller = imgui_ctx->get_focused_controller();
		if (controller < haptic_output.size())
			application::haptic_start(haptic_output[controller], XR_NULL_PATH, 10'000'000, 1000, 1);
	}

	// Render the GUI to the imgui material
	imgui_material->base_color_texture->image_view = imgui_ctx->render();
	// imgui_material->emissive_texture->image_view = imgui_ctx->render();
	imgui_material->ds_dirty = true;
}

void scenes::lobby::render()
{
	if (next_scene && !next_scene->alive())
		next_scene.reset();

	if (next_scene)
	{
		if (next_scene->ready())
		{
			application::push_scene(next_scene);
			next_scene.reset();
		}
	}

	update_server_list();

	XrFrameState framestate = session.wait_frame();

	if (!framestate.shouldRender)
	{
		session.begin_frame();
		session.end_frame(framestate.predictedDisplayTime, {});
		return;
	}

	session.begin_frame();

	auto [flags, views] = session.locate_views(viewconfig, framestate.predictedDisplayTime, world_space);
	assert(views.size() == swapchains.size());

	input->apply(world_space, framestate.predictedDisplayTime);

	draw_gui(framestate.predictedDisplayTime);

	std::vector<XrCompositionLayerProjectionView> layer_view;
	std::vector<scene_renderer::frame_info> frames;

	layer_view.reserve(views.size());
	frames.reserve(views.size());

	for (auto && [view, swapchain]: utils::zip(views, swapchains))
	{
		int image_index = swapchain.acquire();
		swapchain.wait();

		frames.push_back({.destination = swapchain.images()[image_index].image,
		                  .projection = projection_matrix(view.fov),
		                  .view = view_matrix(view.pose)});

		layer_view.push_back({
		        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
		        .pose = view.pose,
		        .fov = view.fov,
		        .subImage = {
		                .swapchain = swapchain,
		                .imageRect = {
		                        .offset = {0, 0},
		                        .extent = swapchain.extent()}},
		});
	}

	assert(renderer);
	renderer->render(*teapot, frames);

	for (auto & swapchain: swapchains)
		swapchain.release();

	XrCompositionLayerProjection layer{
	        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	        .layerFlags = 0,
	        .space = world_space,
	        .viewCount = (uint32_t)layer_view.size(),
	        .views = layer_view.data(),
	};

	std::vector<XrCompositionLayerBaseHeader *> layers_base;
	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer));

	session.end_frame(framestate.predictedDisplayTime, layers_base);
}

void scenes::lobby::on_focused()
{
	discover.emplace(discover_service);
	move_gui_first_time = true;

	auto views = system.view_configuration_views(viewconfig);

	swapchains.reserve(views.size());
	for (auto view: views)
	{
		swapchains.emplace_back(session, device, swapchain_format, view.recommendedImageRectWidth, view.recommendedImageRectHeight);

		spdlog::info("Created lobby swapchain {}: {}x{}", swapchains.size(), swapchains.back().width(), swapchains.back().height());
	}

	uint32_t width = swapchains[0].width();
	uint32_t height = swapchains[0].height();
	vk::Extent2D output_size{width, height};

	std::array depth_formats{
	        vk::Format::eD16Unorm,
	        vk::Format::eX8D24UnormPack32,
	        vk::Format::eD32Sfloat,
	};

	renderer.emplace(device, physical_device, queue, commandpool, output_size, swapchains[0].format(), depth_formats);

	scene_loader loader(device, physical_device, queue, application::queue_family_index(), renderer->get_default_material());

	teapot.emplace();
	teapot->import(loader("ground.gltf"));

	input = input_profile("controllers/" + choose_webxr_profile() + "/profile.json", loader, *teapot);
	spdlog::info("Loaded input profile {}", input->id);

	// Put the imgui node last so that alpha blending works correctly
	teapot->import(loader("imgui.gltf"));
	imgui_material = teapot->find_material("imgui");
	assert(imgui_material);
	imgui_material->shader_name = "unlit";
	imgui_material->blend_enable = true;

	imgui_node = teapot->find_node("imgui");
	assert(imgui_node);

	std::array imgui_inputs{
		imgui_context::controller{
			.aim     = get_action_space("left_aim"),
			.trigger = get_action("left_trigger").first,
			.squeeze = get_action("left_squeeze").first,
			.scroll  = get_action("left_scroll").first,
		},
		imgui_context::controller{
			.aim     = get_action_space("right_aim"),
			.trigger = get_action("right_trigger").first,
			.squeeze = get_action("right_squeeze").first,
			.scroll  = get_action("right_scroll").first,
		},
	};
	imgui_ctx.emplace(device, queue_family_index, queue, world_space, imgui_inputs, 1000, imgui_node->scale);
	imgui_ctx->set_position(imgui_node->translation, imgui_node->rotation);
}

void scenes::lobby::on_unfocused()
{
	discover.reset();

	renderer->wait_idle(); // Must be before the scene data because the renderer uses its descriptor sets

	imgui_ctx.reset();
	imgui_material.reset();
	teapot.reset(); // Must be reset before the renderer so that the descriptor sets are freed before their pools
	renderer.reset();
	swapchains.clear();
}

scene::meta& scenes::lobby::get_meta_scene()
{
	static meta m{
		.name = "Lobby",
		.actions = {
			{"left_aim",      XR_ACTION_TYPE_POSE_INPUT},
			{"left_trigger",  XR_ACTION_TYPE_FLOAT_INPUT},
			{"left_squeeze",  XR_ACTION_TYPE_FLOAT_INPUT},
			{"left_scroll",   XR_ACTION_TYPE_VECTOR2F_INPUT},
			{"left_haptic",   XR_ACTION_TYPE_VIBRATION_OUTPUT},
			{"right_aim",     XR_ACTION_TYPE_POSE_INPUT},
			{"right_trigger", XR_ACTION_TYPE_FLOAT_INPUT},
			{"right_squeeze", XR_ACTION_TYPE_FLOAT_INPUT},
			{"right_scroll",  XR_ACTION_TYPE_VECTOR2F_INPUT},
			{"right_haptic",  XR_ACTION_TYPE_VIBRATION_OUTPUT},
		},
		.bindings = {
			suggested_binding{
				"/interaction_profiles/oculus/touch_controller",
				{
					{"left_aim",      "/user/hand/left/input/aim/pose"},
					{"left_trigger",  "/user/hand/left/input/trigger/value"},
					{"left_squeeze",  "/user/hand/left/input/squeeze/value"},
					{"left_scroll",   "/user/hand/left/input/thumbstick"},
					{"left_haptic",   "/user/hand/left/output/haptic"},
					{"right_aim",     "/user/hand/right/input/aim/pose"},
					{"right_trigger", "/user/hand/right/input/trigger/value"},
					{"right_squeeze", "/user/hand/right/input/squeeze/value"},
					{"right_scroll",  "/user/hand/right/input/thumbstick"},
					{"right_haptic",  "/user/hand/right/output/haptic"},
				}
			},
			suggested_binding{
				"/interaction_profiles/bytedance/pico_neo3_controller",
				{
					{"left_aim",      "/user/hand/left/input/aim/pose"},
					{"left_trigger",  "/user/hand/left/input/trigger/value"},
					{"left_squeeze",  "/user/hand/left/input/squeeze/value"},
					{"left_scroll",   "/user/hand/left/input/thumbstick"},
					{"left_haptic",   "/user/hand/left/output/haptic"},
					{"right_aim",     "/user/hand/right/input/aim/pose"},
					{"right_trigger", "/user/hand/right/input/trigger/value"},
					{"right_squeeze", "/user/hand/right/input/squeeze/value"},
					{"right_scroll",  "/user/hand/right/input/thumbstick"},
					{"right_haptic",  "/user/hand/right/output/haptic"},
				}
			},
			suggested_binding{
				"/interaction_profiles/bytedance/pico4_controller",
				{
					{"left_aim",      "/user/hand/left/input/aim/pose"},
					{"left_trigger",  "/user/hand/left/input/trigger/value"},
					{"left_squeeze",  "/user/hand/left/input/squeeze/value"},
					{"left_scroll",   "/user/hand/left/input/thumbstick"},
					{"left_haptic",   "/user/hand/left/output/haptic"},
					{"right_aim",     "/user/hand/right/input/aim/pose"},
					{"right_trigger", "/user/hand/right/input/trigger/value"},
					{"right_squeeze", "/user/hand/right/input/squeeze/value"},
					{"right_scroll",  "/user/hand/right/input/thumbstick"},
					{"right_haptic",  "/user/hand/right/output/haptic"},
				}
			},
			suggested_binding{
				"/interaction_profiles/htc/vive_focus3_controller",
				{
					{"left_aim",      "/user/hand/left/input/aim/pose"},
					{"left_trigger",  "/user/hand/left/input/trigger/value"},
					{"left_squeeze",  "/user/hand/left/input/squeeze/value"},
					{"left_scroll",   "/user/hand/left/input/thumbstick"},
					{"left_haptic",   "/user/hand/left/output/haptic"},
					{"right_aim",     "/user/hand/right/input/aim/pose"},
					{"right_trigger", "/user/hand/right/input/trigger/value"},
					{"right_squeeze", "/user/hand/right/input/squeeze/value"},
					{"right_scroll",  "/user/hand/right/input/thumbstick"},
					{"right_haptic",  "/user/hand/right/output/haptic"},
				}
			},
			suggested_binding{
				"/interaction_profiles/khr/simple_controller",
				{
					{"left_aim",      "/user/hand/left/input/aim/pose"},
					{"left_trigger",  "/user/hand/left/input/select/click"},
					{"left_squeeze",  "/user/hand/left/input/menu/click"},
					{"right_aim",     "/user/hand/right/input/aim/pose"},
					{"right_trigger", "/user/hand/right/input/select/click"},
					{"right_squeeze", "/user/hand/right/input/menu/click"},
				}
			},
		}
	};

	return m;
}
