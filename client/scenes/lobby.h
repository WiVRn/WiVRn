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

#include "asset.h"
#include "configuration.h"
#include "crypto.h"
#include "render/scene_data.h"
#include "scene.h"
#include "scenes/hand_model.h"
#include "scenes/lobby_keyboard.h"
#include "utils/thread_safe.h"
#include "wifi_lock.h"
#include "wivrn_config.h"
#include "wivrn_discover.h"
#include <vulkan/vulkan_raii.hpp>

#include "xr/system.h"
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
	std::optional<wivrn_discover> discover;
	wifi_lock::multicast multicast;

	std::string add_server_window_prettyname;
	std::string add_server_window_hostname;
	int add_server_window_port = wivrn::default_port;
	bool add_server_tcp_only = false;
	std::string add_server_cookie;

	utils::future<std::unique_ptr<wivrn_session>, std::string> async_session;
	std::optional<std::string> async_error;
	std::shared_ptr<stream> next_scene;
	std::string server_name;
	bool autoconnect_enabled = true;

	std::optional<scene_renderer> renderer;
	std::optional<scene_data> lobby_scene;
	std::optional<scene_data> controllers_scene;
	std::optional<input_profile> input;
	std::optional<hand_model> left_hand;
	std::optional<hand_model> right_hand;
	node_handle lobby_handle;
	bool composition_layer_depth_test_supported;
	bool composition_layer_color_scale_bias_supported;

	std::optional<imgui_context> imgui_ctx;
	std::array<XrAction, 2> haptic_output;

	std::string selected_item;
	std::unique_ptr<asset> license;
	ImGuiID hovered_item;

	std::vector<xr::swapchain> swapchains_lobby;
	std::vector<xr::swapchain> swapchains_lobby_depth;
	std::vector<xr::swapchain> swapchains_controllers;
	std::vector<xr::swapchain> swapchains_controllers_depth;
	xr::swapchain swapchain_imgui;
	vk::Format swapchain_format;
	vk::Format depth_format;
	xr::system::passthrough_type passthrough_supported;
	XrViewConfigurationView stream_view;

#if WIVRN_CLIENT_DEBUG_MENU
	// GUI debug
	node_handle xyz_axes_left_controller;
	node_handle xyz_axes_right_controller;
	bool display_debug_axes = false;
	bool display_grip_instead_of_aim = false;
	glm::vec3 offset_position{};
	glm::vec3 offset_orientation{};
	float ray_offset{};
#endif

	void update_server_list();

	std::vector<std::pair<int, XrCompositionLayerQuad>> draw_gui(XrTime predicted_display_time);

	XrAction recenter_left_action = XR_NULL_HANDLE;
	XrAction recenter_right_action = XR_NULL_HANDLE;
	std::optional<glm::vec3> gui_recenter_position;
	std::optional<float> gui_recenter_distance;
	bool recenter_gui = true;
	void move_gui(glm::vec3 head_position, glm::vec3 new_gui_position);
	void tooltip(std::string_view text);

	enum class tab
	{
		server_list,
		settings,
#if WIVRN_CLIENT_DEBUG_MENU
		debug,
#endif
		about,
		licenses,
		exit
	};

	tab current_tab = tab::server_list;
	tab last_current_tab = tab::server_list;
	ImTextureID about_picture;

	virtual_keyboard keyboard;

	struct pin_request_data
	{
		bool pin_requested = false;
		bool pin_cancelled = false;
		std::string pin;
	};

	thread_safe_notifyable<pin_request_data> pin_request;
	std::string pin_buffer;

	void draw_features_status(XrTime predicted_display_time);
	void gui_connecting(locked_notifiable<pin_request_data> & request);
	void gui_enter_pin(locked_notifiable<pin_request_data> & request);
	void gui_server_list();
	void gui_new_server();
	void gui_settings();
	void gui_debug();
	void gui_about();
	void gui_licenses();
	void gui_keyboard();

	void setup_passthrough();

	void vibrate_on_hover();

	void connect(const configuration::server_data & data);
	std::unique_ptr<wivrn_session> connect_to_session(wivrn_discover::service service, bool manual_connection);

	std::optional<glm::vec3> check_recenter_gesture(const std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT> & joints);
	std::optional<glm::vec3> check_recenter_action(XrTime predicted_display_time, glm::vec3 head_position);
	std::optional<glm::vec3> check_recenter_gui(glm::vec3 head_position, glm::quat head_orientation);

	crypto::key keypair;

public:
	virtual ~lobby();
	lobby();

	void render(const XrFrameState &) override;
	void on_unfocused() override;
	void on_focused() override;
	void on_xr_event(const xr::event &) override;

	static meta & get_meta_scene();
};
} // namespace scenes
