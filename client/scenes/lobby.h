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

#include "configuration.h"
#include "crypto.h"
#include "file_picker.h"
#include "libcurl.h"
#include "scene.h"
#include "scenes/lobby_keyboard.h"
#include "utils/mapped_file.h"
#include "utils/thread_safe.h"
#include "wifi_lock.h"
#include "wivrn_config.h"
#include "wivrn_discover.h"
#include "xr/face_tracker.h"
#include "xr/foveation_profile.h"
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>

#include <compare>
#include <optional>
#include <uni_algo/case.h>
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

	file_picker lobby_file_picker;
	std::future<file_picker_result> lobby_file_picker_future;

	std::optional<imgui_context> imgui_ctx;

	std::optional<xr::hand_tracker> left_hand;
	std::optional<xr::hand_tracker> right_hand;

	xr::face_tracker face_tracker;

	std::optional<xr::foveation_profile> foveation;

	std::string selected_item;
	std::unique_ptr<utils::mapped_file> license;

	static inline const uint32_t layer_lobby = 1 << 0;
	static inline const uint32_t layer_controllers = 1 << 1;
	static inline const uint32_t layer_rays = 1 << 2;

	uint32_t width;
	uint32_t height;
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
		customize,
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

	struct environment_model
	{
		// Data from the JSON
		std::string name;
		std::string author;
		std::string description;
		std::string screenshot_url;
		std::string gltf_url;
		size_t size{};

		bool builtin = false;
		int override_order = 0;
		std::filesystem::path local_screenshot_path;
		std::filesystem::path local_gltf_path;
		std::vector<std::byte> screenshot_png;
		ImTextureID screenshot{};

		std::strong_ordering operator<=>(const environment_model & other) const
		{
			// Reverse order for the builtin member: we want the built-in models first
			if (auto cmp = override_order <=> other.override_order; cmp != std::strong_ordering::equal)
				return cmp;

			return una::casesens::collate_utf8(name, other.name) <=> 0;
		}
	};

	ImTextureID default_environment_screenshot;
	std::vector<environment_model> downloadable_environments;
	std::vector<environment_model> local_environments;
	environment_model * environment_to_be_deleted = nullptr;

	libcurl curl; // needs to be before current_transfers
	std::map<std::string, std::pair<libcurl::curl_handle, std::function<void(libcurl::curl_handle & handle)>>> current_transfers;

	void update_file_picker();
	void update_transfers();
	void download(const std::string & url, const std::filesystem::path & path, std::function<void(libcurl::curl_handle & handle)> = {});
	void download(const std::string & url, std::function<void(libcurl::curl_handle & handle)> = {});
	libcurl::curl_handle * try_get_download_handle(const std::string & url);

	std::string downloadable_environment_list_status;

	std::vector<environment_model> load_environment_json(const std::string & json, std::string_view base_url = "");
	void save_environment_json();
	void download_environment(const environment_model & model, bool use_after_downloading);
	void delete_environment(const environment_model & model);
	enum class environment_item_action
	{
		none,
		delete_model,
		download_model,
		use_model,
	};
	environment_item_action environment_item(environment_model & model, bool download_screenshot);
	void environment_list(std::vector<environment_model> & model, bool download_screenshot);
	void use_environment(const environment_model & model);
	XrTime popup_load_environment_display_time = 0;
	void popup_load_environment(XrTime predicted_display_time);
	utils::future<std::pair<std::string, std::shared_ptr<entt::registry>>, float> future_environment;
	std::string load_environment_status;

	void download_environment_list();
	libcurl::curl_handle * parse_environment_list();

#if WIVRN_CLIENT_DEBUG_MENU
	std::pair<entt::entity, int> debug_primitive_to_highlight = {entt::null, 0};
#endif

	void draw_features_status(XrTime predicted_display_time);
	void gui_connecting(locked_notifiable<pin_request_data> & request);
	void gui_enter_pin(locked_notifiable<pin_request_data> & request);
	void gui_connected(XrTime predicted_display_time);
	void gui_server_list();
	void gui_new_server();
	void gui_settings();
	void gui_post_processing();
	void gui_customize(XrTime predicted_display_time);
	void gui_debug_node_hierarchy(entt::entity root = entt::null);
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
