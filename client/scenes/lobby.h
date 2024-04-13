/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "render/scene_data.h"
#include "scene.h"
#include "scenes/hand_model.h"
#include "wivrn_discover.h"
#include <vulkan/vulkan_raii.hpp>

#include "xr/passthrough.h"
#include <map>
#include <optional>
#include <vector>

#include "input_profile.h"
#include "render/imgui_impl.h"
#include "render/scene_renderer.h"
#include "utils/async.h"

class wivrn_session;

namespace scenes
{
class stream;

class lobby : public scene_impl<lobby>
{
	struct server_data
	{
		bool autoconnect;
		bool manual;
		bool visible;
		bool compatible;

		wivrn_discover::service service;
	};

	enum class connection_status
	{
		idle,
		connecting,
		connected,
		error,
	};

	std::optional<wivrn_discover> discover;
	std::map<std::string, server_data> servers;

	char add_server_window_prettyname[200];
	char add_server_window_hostname[200];
	int add_server_window_port;

	utils::future<std::unique_ptr<wivrn_session>, std::string> async_session;
	std::optional<std::string> async_error;
	std::shared_ptr<stream> next_scene;
	std::string server_name;

	std::optional<scene_renderer> renderer;
	std::optional<scene_data> lobby_scene;
	std::optional<scene_data> controllers_scene;
	std::optional<input_profile> input;
	std::optional<hand_model> left_hand;
	std::optional<hand_model> right_hand;

	std::optional<imgui_context> imgui_ctx;
	std::array<XrAction, 2> haptic_output;

	std::string selected_item;
	ImGuiID hovered_item;

	std::vector<xr::swapchain> swapchains_lobby;
	std::vector<xr::swapchain> swapchains_controllers;
	xr::swapchain swapchain_imgui;
	vk::Format swapchain_format;
	xr::system::passthrough_type passthrough_supported;
	xr::passthrough passthrough;

	void save_config();

	void update_server_list();

	XrCompositionLayerQuad draw_gui(XrTime predicted_display_time);

	XrAction recenter_left_action = XR_NULL_HANDLE;
	XrAction recenter_right_action = XR_NULL_HANDLE;
	bool recenter_gui = true;
	void move_gui(glm::vec3 head_position, glm::vec3 new_gui_position);

	enum class tab
	{
		server_list,
		new_server,
		settings,
		about,
		exit
	};

	tab current_tab = tab::server_list;
	tab last_current_tab = tab::server_list;
	ImTextureID about_picture;

	float preferred_refresh_rate = 0;
	bool show_performance_metrics = false;
	bool microphone = false;
	bool passthrough_enabled = false;

	void gui_connecting();
	void gui_server_list();
	void gui_add_server();
	void gui_settings();
	void gui_about();
	void gui_keyboard(ImVec2 size);

	void setup_passthrough();

	void vibrate_on_hover();

	void connect(server_data & data);

	std::optional<glm::vec3> check_recenter_gesture(const std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT> & joints);
	std::optional<glm::vec3> check_recenter_action(XrTime predicted_display_time);
	std::optional<glm::vec3> check_recenter_gui(glm::vec3 head_position, glm::quat head_orientation);

public:
	virtual ~lobby();
	lobby();

	void render(XrTime predicted_display_time, bool should_render) override;
	void on_unfocused() override;
	void on_focused() override;
	void on_session_state_changed(XrSessionState state) override;

	static meta & get_meta_scene();
};
} // namespace scenes
