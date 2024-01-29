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
#include <vulkan/vulkan_raii.hpp>
#include "wivrn_discover.h"

#include <deque>
#include <optional>
#include <vector>
#include <map>
#include <future>

#include "render/scene_renderer.h"
#include "render/imgui_impl.h"
#include "input_profile.h"

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

	std::future<std::unique_ptr<wivrn_session>> async_session;
	std::shared_ptr<stream> next_scene;
	std::string server_name;
	std::string next_scene_status_string;
	connection_status next_scene_status = connection_status::idle;
	int next_scene_status_token = 0;
	std::mutex next_scene_status_lock;
	std::deque<std::future<std::unique_ptr<wivrn_session>>> discarded_futures;

	void set_next_scene_status(std::string status_str, connection_status status, int token);
	std::pair<std::string, connection_status> get_next_scene_status();

	std::optional<scene_renderer> renderer;
	std::optional<scene_data> teapot;
	std::optional<input_profile> input;

	std::optional<imgui_context> imgui_ctx;
	std::shared_ptr<scene_data::material> imgui_material;
	scene_object_handle imgui_node;
	std::array<XrAction, 2> haptic_output;

	std::string selected_item;
	std::string hovered_item;

	std::vector<xr::swapchain> swapchains;
	vk::Format swapchain_format;

	void save_config();

	void update_server_list();

	void draw_gui(XrTime predicted_display_time);

	bool move_gui_first_time = true;

	void move_gui(glm::vec3 position, glm::quat orientation, XrTime predicted_display_time);

	void gui_connecting();
	void gui_server_list(bool open_connecting_popup);
	void gui_add_server();
	void gui_keyboard(ImVec2 size);

	void connect(server_data& data);

public:
	virtual ~lobby();
	lobby();

	void render() override;
	void render_view(XrViewStateFlags flags, XrTime display_time, XrView & view, int swapchain_index, int image_index);

	void on_unfocused() override;
	void on_focused() override;

	static meta& get_meta_scene();
};
} // namespace scenes
