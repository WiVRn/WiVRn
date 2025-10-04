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
#include "scene.h"
#include "scenes/lobby_keyboard.h"
#include "utils/thread_safe.h"
#include "wifi_lock.h"
#include "wivrn_config.h"
#include "wivrn_discover.h"
#include "xr/face_tracker.h"
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>

#include <optional>
#include <vector>

#include "input_profile.h"
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

	std::optional<input_profile> input;
	entt::entity lobby_entity;

	std::optional<imgui_context> imgui_ctx;

	std::optional<xr::hand_tracker> left_hand;
	std::optional<xr::hand_tracker> right_hand;

	xr::face_tracker face_tracker;

	std::string selected_item;
	std::unique_ptr<asset> license;

	static inline const uint32_t layer_lobby = 1 << 0;
	static inline const uint32_t layer_controllers = 1 << 1;
	static inline const uint32_t layer_rays = 1 << 2;

	uint32_t width;
	uint32_t height;
	xr::swapchain swapchain_imgui;
	XrViewConfigurationView stream_view;

#if WIVRN_CLIENT_DEBUG_MENU
	// GUI debug
	entt::entity xyz_axes_left_controller;
	entt::entity xyz_axes_right_controller;
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
	// std::optional<glm::vec3> gui_recenter_position;
	// std::optional<float> gui_recenter_distance;
	// Which controller is used for recentering, position of the pointed point in the GUI, in GUI axes, and distance between the controller and the pointed point during recentering
	std::optional<std::tuple<xr::spaces, glm::vec3, float>> recentering_context;
	bool recenter_gui = true;
	void move_gui(glm::vec3 head_position, glm::vec3 new_gui_position);

	enum class tab
	{
		first_run,
		server_list,
		settings,
		post_processing,
#if WIVRN_CLIENT_DEBUG_MENU
		debug,
#endif
		about,
		licenses,
		exit,
		connected,
	};

	tab current_tab = tab::server_list;
	tab last_current_tab = tab::server_list;
	int optional_feature_index = 0; // Which step of the first run screen are we in
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
	void gui_connected(XrTime predicted_display_time);
	void gui_server_list();
	void gui_new_server();
	void gui_settings();
	void gui_post_processing();
	void gui_debug();
	void gui_about();
	void gui_licenses();
	void gui_keyboard();
	void gui_first_run();

	void setup_passthrough();

	void connect(const configuration::server_data & data);
	std::unique_ptr<wivrn_session> connect_to_session(wivrn_discover::service service, bool manual_connection);

	std::optional<glm::vec3> check_recenter_gesture(xr::spaces space, const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & joints);
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
