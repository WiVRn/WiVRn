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
#include "application.h"
#include "configuration.h"
#include "constants.h"
#include "glm/geometric.hpp"
#include "hand_model.h"
#include "hardware.h"
#include "imgui.h"
#include "openxr/openxr.h"
#include "protocol_version.h"
#include "render/animation.h"
#include "render/scene_components.h"
#include "stream.h"
#include "utils/files.h"
#include "utils/glm_cast.h"
#include "utils/i18n.h"
#include "wivrn_client.h"
#include "wivrn_discover.h"
#include "wivrn_sockets.h"
#include "xr/passthrough.h"
#include "xr/space.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <glm/ext.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/matrix.hpp>
#include <magic_enum.hpp>
#include <ranges>
#include <simdjson.h>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/poll.h>
#include <vulkan/vulkan_raii.hpp>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "wivrn_config.h"
#if WIVRN_FEATURE_RENDERDOC
#include "vk/renderdoc.h"
#endif

using namespace std::chrono_literals;

static bool force_autoconnect = false;

scenes::lobby::~lobby()
{
	if (renderer)
		renderer->wait_idle();
}

static const std::array supported_color_formats = {
        vk::Format::eR8G8B8A8Srgb,
        vk::Format::eB8G8R8A8Srgb,
};

static const std::array supported_depth_formats{
        vk::Format::eD32Sfloat,
        vk::Format::eX8D24UnormPack32,
};

static float interpolate(float x, std::span<const std::pair<float, float>> arr)
{
	assert(arr.size() >= 1);

	if (x <= arr[0].first)
		return arr[0].second;

	if (x >= arr.back().first)
		return arr.back().second;

	for (size_t i = 0; i + 1 < arr.size(); i++)
	{
		assert(arr[i].first <= arr[i + 1].first);
		if (arr[i].first <= x and x < arr[i + 1].first)
		{
			auto t = (x - arr[i].first) / (arr[i + 1].first - arr[i].first);
			return std::lerp(arr[i].second, arr[i + 1].second, t);
		}
	}

	return arr[0].second; // Should never happen
}

static glm::quat compute_gui_orientation(glm::vec3 head_position, glm::vec3 new_gui_position)
{
	glm::vec3 gui_direction = new_gui_position - head_position;

	float gui_yaw = atan2(gui_direction.x, gui_direction.z) + M_PI;

	float eye_gaze_elevation = atan2(gui_direction.y, glm::length(glm::vec2(gui_direction.x, gui_direction.z)));
	float gui_pitch = interpolate(eye_gaze_elevation * 180 / M_PI, constants::lobby::gui_pitches) * M_PI / 180;

	return glm::quat(cos(gui_yaw / 2), 0, sin(gui_yaw / 2), 0) * glm::quat(cos(gui_pitch / 2), sin(gui_pitch / 2), 0, 0);
}

void scenes::lobby::move_gui(glm::vec3 head_position, glm::vec3 new_gui_position)
{
	using constants::lobby::keyboard_pitch;
	using constants::lobby::keyboard_position;
	using constants::lobby::popup_position;

	auto q = compute_gui_orientation(head_position, new_gui_position);
	auto M = glm::mat3_cast(q); // plane-to-world transform

	// Main window
	imgui_ctx->layers()[0].position = new_gui_position;
	imgui_ctx->layers()[0].orientation = q;

	// Popup
	imgui_ctx->layers()[1].position = new_gui_position + M * popup_position;
	imgui_ctx->layers()[1].orientation = q;

	// Keyboard
	imgui_ctx->layers()[2].position = new_gui_position + M * keyboard_position;
	imgui_ctx->layers()[2].orientation = q * glm::quat(cos(keyboard_pitch / 2), sin(keyboard_pitch / 2), 0, 0);
}

scenes::lobby::lobby() :
        scene_impl<lobby>(supported_color_formats, supported_depth_formats)
{
	spdlog::info("Using formats {} and {}", vk::to_string(swapchain_format), vk::to_string(depth_format));

	if (composition_layer_depth_test_supported)
		spdlog::info("Composition layer depth test supported");
	else
		spdlog::info("Composition layer depth test NOT supported");

	if (composition_layer_color_scale_bias_supported)
		spdlog::info("Composition layer color scale/bias supported");
	else
		spdlog::info("Composition layer color scale/bias NOT supported");

	if (instance.has_extension(XR_FB_FOVEATION_VULKAN_EXTENSION_NAME) and
	    instance.has_extension(XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME))
	{
		spdlog::info("Foveation image supported");
		foveation = xr::foveation_profile(instance, session, XR_FOVEATION_LEVEL_NONE_FB, -10, false);
	}
	else
		spdlog::info("Foveation image NOT supported");

	if (std::getenv("WIVRN_AUTOCONNECT"))
		force_autoconnect = true;

	auto & config = application::get_config();

	auto & servers = config.servers;
	spdlog::info("{} known server(s):", servers.size());
	for (auto & i: servers)
	{
		spdlog::info("    {}", i.second.service.name);
	}

	keyboard.set_layout(config.virtual_keyboard_layout);

	if (config.first_run)
		current_tab = tab::first_run;

	const auto keypair_path = application::get_config_path() / "private_key.pem";
	try
	{
		std::ifstream f{keypair_path};
		keypair = crypto::key::from_private_key(std::string{(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()});
	}
	catch (...)
	{
		keypair = crypto::key::generate_x448_keypair();
		std::ofstream{keypair_path} << keypair.private_key();
		spdlog::info("Generated X448 keypair");
	}
}

static std::string ip_address_to_string(const in_addr & addr)
{
	char buf[100];
	inet_ntop(AF_INET, &addr, buf, sizeof(buf));
	return buf;
}

static std::string ip_address_to_string(const in6_addr & addr)
{
	char buf[100];
	inet_ntop(AF_INET6, &addr, buf, sizeof(buf));
	return buf;
}

std::unique_ptr<wivrn_session> scenes::lobby::connect_to_session(wivrn_discover::service service, bool manual_connection)
{
	if (!manual_connection)
	{
		char protocol_string[17];
		sprintf(protocol_string, "%016lx", wivrn::protocol_version);

		spdlog::debug("Client protocol version: {}", protocol_string);
		spdlog::debug("Server TXT:");
		for (auto & [i, j]: service.txt)
		{
			spdlog::debug("    {}=\"{}\"", i, j);
		}

		auto protocol = service.txt.find("protocol");
		if (protocol == service.txt.end())
			throw std::runtime_error(_("Incompatible WiVRn server: no protocol field in TXT"));

		if (protocol->second != protocol_string)
			throw std::runtime_error(fmt::format(_F("Incompatible WiVRn server protocol (client: {}, server: {})"), protocol_string, protocol->second));
	}

	// Only the automatically discovered servers already have their IP addresses available
	if (manual_connection)
	{
		addrinfo hint{
		        .ai_flags = AI_ADDRCONFIG,
		        .ai_family = AF_UNSPEC,
		        .ai_socktype = SOCK_STREAM,
		};
		addrinfo * addresses;
		if (int err = getaddrinfo(service.hostname.c_str(), nullptr, &hint, &addresses))
		{
			spdlog::error("Cannot resolve hostname {}: {}", service.hostname, gai_strerror(err));
			throw std::runtime_error(fmt::format(_F("Cannot resolve hostname: {}"), _(gai_strerror(err))));
		}

		for (auto i = addresses; i; i = i->ai_next)
		{
			switch (i->ai_family)
			{
				case AF_INET:
					service.addresses.push_back(((sockaddr_in *)i->ai_addr)->sin_addr);
					break;
				case AF_INET6:
					service.addresses.push_back(((sockaddr_in6 *)i->ai_addr)->sin6_addr);
					break;
			}
		}

		freeaddrinfo(addresses);
	}

	std::string error;
	for (const std::variant<in_addr, in6_addr> & address: service.addresses)
	{
		std::string address_string = std::visit([](auto & address) {
			return ip_address_to_string(address);
		},
		                                        address);

		struct connection_cancelled
		{};

		try
		{
			spdlog::debug("Connection to {}", address_string);

			return std::visit([this, &service](auto & address) {
				return std::make_unique<wivrn_session>(address, service.port, service.tcp_only, keypair, [&](int fd) {
					auto request = pin_request.lock();
					request->pin_requested = true;
					request->pin_cancelled = false;
					request->pin = service.pin;
					pin_buffer = "";

					while (not request.wait_for(500ms, [&]() { return request->pin != "" or request->pin_cancelled; }))
					{
						pollfd fds{};
						fds.events = POLLRDHUP;
						fds.fd = fd;

						int r = ::poll(&fds, 1, 0);

						if (r < 0)
						{
							request->pin_requested = false;
							throw std::system_error(errno, std::system_category());
						}

						if (fds.revents & (POLLHUP | POLLERR))
						{
							request->pin_requested = false;
							throw std::runtime_error("Error on control socket");
						}

						if (fds.revents & POLLRDHUP)
						{
							request->pin_requested = false;
							throw socket_shutdown{};
						}
					}

					request->pin_requested = false;

					if (request->pin_cancelled)
						throw connection_cancelled{};

					return request->pin;
				});
			},
			                  address);
		}
		catch (connection_cancelled)
		{
			spdlog::info("Connection cancelled");
			return nullptr;
		}
		catch (handshake_error & e)
		{
			spdlog::warn("Error during handshake to {} ({}): {}", service.hostname, address_string, e.what());
			std::string txt = fmt::format(_F("Cannot connect to {} ({}): {}"), service.hostname, address_string, e.what());
			throw std::runtime_error(txt);
		}
		catch (std::exception & e)
		{
			spdlog::warn("Cannot connect to {} ({}): {}", service.hostname, address_string, e.what());
			std::string txt = fmt::format(_F("Cannot connect to {} ({}): {}"), service.hostname, address_string, e.what());
			if (not error.empty())
				error += "\n";
			error += txt;
		}
	}

	throw std::runtime_error(error);
}

void scenes::lobby::update_server_list()
{
	if (application::is_focused() && !discover)
		discover.emplace();
	else if (!application::is_focused() && discover)
		discover.reset();

	if (!discover)
		return;

	std::vector<wivrn_discover::service> discovered_services = discover->get_services();

	// TODO: only if discovered_services changed
	auto & servers = application::get_config().servers;
	for (auto & [cookie, data]: servers)
	{
		data.visible = false;
	}

	char protocol_string[17];
	sprintf(protocol_string, "%016lx", wivrn::protocol_version);

	for (auto & service: discovered_services)
	{
		std::string cookie;
		bool compatible = true;

		auto it = service.txt.find("cookie");
		if (it == service.txt.end())
		{
			cookie = service.hostname;
			compatible = false;
		}
		else
			cookie = it->second;

		auto protocol = service.txt.find("protocol");
		if (protocol == service.txt.end())
			compatible = false;

		if (protocol->second != protocol_string)
			compatible = false;

		auto server = servers.find(cookie);
		if (server == servers.end())
		{
			// Newly discovered server: add it to the list
			servers.emplace(
			        cookie,
			        configuration::server_data{
			                .autoconnect = false,
			                .manual = false,
			                .visible = true,
			                .compatible = compatible,
			                .service = service,
			        });
		}
		else
		{
			server->second.visible = true;
			server->second.service = service;
			server->second.compatible = compatible;
		}
	}
}

void scenes::lobby::connect(const configuration::server_data & data)
{
	server_name = data.service.name;
	async_error.reset();

	async_session = utils::async<std::unique_ptr<wivrn_session>, std::string>(
	        [this](auto token, wivrn_discover::service service, bool manual) {
		        token.set_progress(_("Waiting for connection"));
		        return connect_to_session(service, manual);
	        },
	        data.service,
	        data.manual);
}

std::optional<glm::vec3> scenes::lobby::check_recenter_gesture(
        xr::spaces space,
        const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & joints,
        const std::pair<glm::vec3, glm::quat> & head_pose)
{
	if (recentering_context and std::get<0>(*recentering_context) != space)
		return std::nullopt;

	if (joints)
	{
		const auto & palm = (*joints)[XR_HAND_JOINT_PALM_EXT].first;
		const auto palm_y = glm::rotate(glm_cast(palm.pose.orientation), glm::vec3(0, 1, 0));

		for (XrHandJointEXT index: {
		             XR_HAND_JOINT_INDEX_PROXIMAL_EXT,
		             XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT,
		             XR_HAND_JOINT_INDEX_DISTAL_EXT,

		             XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,
		             XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT,
		             XR_HAND_JOINT_MIDDLE_DISTAL_EXT,

		             XR_HAND_JOINT_RING_PROXIMAL_EXT,
		             XR_HAND_JOINT_RING_INTERMEDIATE_EXT,
		             XR_HAND_JOINT_RING_DISTAL_EXT,

		             XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,
		             XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT,
		             XR_HAND_JOINT_LITTLE_DISTAL_EXT,
		     })
		{
			const auto fingertip_y = glm::rotate(glm_cast((*joints)[index].first.pose.orientation), glm::vec3(0, 1, 0));

			if (glm::dot(palm_y, fingertip_y) < constants::lobby::recenter_cos_fingertip_angle_max)
			{
				recentering_context.reset();
				return std::nullopt;
			}
		}

		const auto q = glm_cast(palm.pose.orientation);
		const auto x = glm_cast(palm.pose.position);

		// Make the up vector orthogonal to forward
		const glm::vec3 forward = glm::rotate(head_pose.second, glm::vec3{0, 0, -1});
		const glm::vec3 up = glm::normalize(glm::vec3{0, 1, 0} - forward * glm::dot(forward, {0, 1, 0}));

		// Y is pointing to the back of the hand
		if (glm::dot(q * glm::vec3(0, -1, 0), up) > constants::lobby::recenter_cos_palm_angle_min)
		{
			recentering_context.emplace(space, glm::vec3{}, 0);
			return x + glm::rotate(q, glm::vec3{0, -constants::lobby::recenter_distance_up, -constants::lobby::recenter_distance_front});
		}
	}

	recentering_context.reset();
	return std::nullopt;
}

std::optional<glm::vec3> scenes::lobby::check_recenter_action(XrTime predicted_display_time, glm::vec3 head_position)
{
	xr::spaces controller;
	glm::vec3 recenter_position;
	float recenter_distance;

	if (recentering_context)
	{
		controller = std::get<0>(*recentering_context);
		bool state;
		switch (controller)
		{
			case xr::spaces::aim_left:
				state = application::read_action_bool(recenter_left_action).value_or(std::pair{0, false}).second;
				break;

			case xr::spaces::aim_right:
				state = application::read_action_bool(recenter_right_action).value_or(std::pair{0, false}).second;
				break;

			default:
				return std::nullopt;
		}

		if (not state)
		{
			recentering_context.reset();
			return std::nullopt;
		}
	}
	else if (auto state = application::read_action_bool(recenter_left_action); state and state->second)
		controller = xr::spaces::aim_left;
	else if (auto state = application::read_action_bool(recenter_right_action); state and state->second)
		controller = xr::spaces::aim_right;
	else
		return std::nullopt;

	auto aim = application::locate_controller(application::space(controller), application::space(xr::spaces::world), predicted_display_time);

	if (not aim)
	{
		// The controller cannot be located
		recentering_context.reset();
		return std::nullopt;
	}

	// Handle controller offset
	auto [offset_position, offset_orientation] = input->offset[controller];
	aim->first = aim->first + glm::mat3_cast(aim->second * offset_orientation) * offset_position;
	aim->second = aim->second * offset_orientation;

	if (not recentering_context)
	{
		// First frame of recentering
		imgui_context::controller_state state{
		        .aim_position = aim->first,
		        .aim_orientation = aim->second,
		};

		state.pointer_position = imgui_ctx->compute_pointer_position(state).first;

		if (state.pointer_position) // TODO: check that the pointer is inside an imgui window
		{
			auto M = glm::mat3_cast(imgui_ctx->layers()[0].orientation);

			// Pointer position in world
			glm::vec3 world_pointer_position = imgui_ctx->rw_from_vp(*state.pointer_position);

			// Pointer position in GUI
			recenter_position = glm::transpose(M) * (world_pointer_position - imgui_ctx->layers()[0].position);
			recenter_distance = glm::length(state.aim_position - world_pointer_position);
		}
		else
		{
			// Use the center of the GUI if the controller points outside of the GUI
			recenter_position = glm::vec3(0, 0, 0);
			recenter_distance = constants::lobby::recenter_action_distance;
		}

		recentering_context.emplace(controller, recenter_position, recenter_distance);
		return std::nullopt;
	}
	else
	{
		// Subsequent frames: find the GUI position that gives the correct world pointer position
		std::tie(controller, recenter_position, recenter_distance) = *recentering_context;

		glm::vec3 controller_direction = -glm::column(glm::mat3_cast(aim->second), 2);
		glm::vec3 wanted_world_pointer_position = aim->first + controller_direction * recenter_distance;

		// I'm sure there's an analytical solution but I can't be bothered to write it so
		// let's use a gradient descent instead
		auto f = [&](glm::vec3 new_gui_position) {
			auto q = compute_gui_orientation(head_position, new_gui_position);
			auto M = glm::mat3_cast(q); // plane-to-world transform

			glm::vec3 world_pointer_position = new_gui_position + M * recenter_position;

			return world_pointer_position - wanted_world_pointer_position;
		};

		// One step is usually enough, the solution will continuously improve in the next frames
		glm::vec3 gui_position = imgui_ctx->layers()[0].position;
		float eps = 0.01;

		glm::vec3 obj;
		int n_iter = 0;
		do
		{
			obj = f(gui_position);
			glm::vec3 obj_dx = (f(gui_position + glm::vec3(eps, 0, 0)) - obj) / eps;
			glm::vec3 obj_dy = (f(gui_position + glm::vec3(0, eps, 0)) - obj) / eps;
			glm::vec3 obj_dz = (f(gui_position + glm::vec3(0, 0, eps)) - obj) / eps;

			glm::mat3 jacobian{obj_dx, obj_dy, obj_dz};

			gui_position -= 0.01 * glm::inverse(jacobian) * obj;
			n_iter++;
		} while (glm::length(obj) > 0.0001 and n_iter < 1000);

		return gui_position;
	}
}

std::optional<glm::vec3> scenes::lobby::check_recenter_gui(glm::vec3 head_position, glm::quat head_orientation)
{
	glm::vec3 head_direction = -glm::column(glm::mat3_cast(head_orientation), 2);

	if (recenter_gui)
	{
		recenter_gui = false;
		glm::vec3 new_gui_position = head_position + constants::lobby::initial_gui_distance * head_direction;
		new_gui_position.y = head_position.y - 0.1;
		return new_gui_position;
	}

	return std::nullopt;
}

static void stick_finger_to_gui(std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT> & hand, const std::vector<imgui_context::window_viewport> & layers)
{
	// Move XR_HAND_JOINT_INDEX_{TIP,DISTAL,INTERMEDIATE,PROXIMAL}_EXT so that:
	// - Only the X rotation changes for distal and intermediate (relative to the parent bone)
	// - Only the X and Y rotation change for the proximal
	// - The orientation of the tip is the same as the distal
	// - The position stays constant in the frame of the parent bone
	// - The position of the tip is on the closest imgui window (considering the tip radius)
	// - The rotation on X for the index proximal is limited to [-90, +30]
	// - The rotation on Y for the index proximal is limited to [-30, +30]
	// - The rotation on X for the index intermediate are equal and limited to [-90, 0]

	// Convert from OpenXR types to glm
	glm::vec3 tip_x{
	        hand[XR_HAND_JOINT_INDEX_TIP_EXT].first.pose.position.x,
	        hand[XR_HAND_JOINT_INDEX_TIP_EXT].first.pose.position.y,
	        hand[XR_HAND_JOINT_INDEX_TIP_EXT].first.pose.position.z};

	// Compute the target position of the tip
	std::vector<std::pair<glm::vec3, float>> intersections; // Target position of the finger tip, distance to the GUI plane
	for (const imgui_context::window_viewport & layer: layers)
	{
		// Ignore layers that are not absolutely positionned
		if (layer.space != xr::spaces::world)
			continue;

		// Compute all vectors in the reference frame of the GUI plane
		// The normal of the GUI plane is +Z, the tip points toward -Z
		auto M = glm::mat3_cast(layer.orientation); // plane-to-world transform
		glm::vec3 ray_start = glm::transpose(M) * (tip_x - layer.position);

		// Ignore this layer if the tip is outside the limits
		if (std::abs(ray_start.x) > layer.size.x / 2)
			continue;

		if (std::abs(ray_start.y) > layer.size.y / 2)
			continue;

		float distance = ray_start.z; // Positive when the point is in front of the plane

		// Not near the plane: ignore
		if (distance > 0 or distance < constants::gui::fingertip_distance_stick_thd)
			continue;

		glm::vec3 target_position = tip_x + M * glm::vec3{0, 0, -distance};

		intersections.emplace_back(target_position, std::abs(distance));
	}

	if (intersections.empty())
		return;

	auto [target_position, distance] = *std::ranges::min_element(intersections, {}, &std::pair<glm::vec3, float>::second);

	// Convert from OpenXR to glm
	glm::vec3 distal_x{
	        hand[XR_HAND_JOINT_INDEX_DISTAL_EXT].first.pose.position.x,
	        hand[XR_HAND_JOINT_INDEX_DISTAL_EXT].first.pose.position.y,
	        hand[XR_HAND_JOINT_INDEX_DISTAL_EXT].first.pose.position.z};
	glm::vec3 intermediate_x{
	        hand[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT].first.pose.position.x,
	        hand[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT].first.pose.position.y,
	        hand[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT].first.pose.position.z};
	glm::vec3 proximal_x{
	        hand[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].first.pose.position.x,
	        hand[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].first.pose.position.y,
	        hand[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].first.pose.position.z};
	glm::vec3 metacarpal_x{
	        hand[XR_HAND_JOINT_INDEX_METACARPAL_EXT].first.pose.position.x,
	        hand[XR_HAND_JOINT_INDEX_METACARPAL_EXT].first.pose.position.y,
	        hand[XR_HAND_JOINT_INDEX_METACARPAL_EXT].first.pose.position.z};
	glm::quat distal_q = glm::quat::wxyz(
	        hand[XR_HAND_JOINT_INDEX_DISTAL_EXT].first.pose.orientation.w,
	        hand[XR_HAND_JOINT_INDEX_DISTAL_EXT].first.pose.orientation.x,
	        hand[XR_HAND_JOINT_INDEX_DISTAL_EXT].first.pose.orientation.y,
	        hand[XR_HAND_JOINT_INDEX_DISTAL_EXT].first.pose.orientation.z);
	glm::quat intermediate_q = glm::quat::wxyz(
	        hand[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT].first.pose.orientation.w,
	        hand[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT].first.pose.orientation.x,
	        hand[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT].first.pose.orientation.y,
	        hand[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT].first.pose.orientation.z);
	glm::quat proximal_q = glm::quat::wxyz(
	        hand[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].first.pose.orientation.w,
	        hand[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].first.pose.orientation.x,
	        hand[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].first.pose.orientation.y,
	        hand[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].first.pose.orientation.z);
	glm::quat metacarpal_q = glm::quat::wxyz(
	        hand[XR_HAND_JOINT_INDEX_METACARPAL_EXT].first.pose.orientation.w,
	        hand[XR_HAND_JOINT_INDEX_METACARPAL_EXT].first.pose.orientation.x,
	        hand[XR_HAND_JOINT_INDEX_METACARPAL_EXT].first.pose.orientation.y,
	        hand[XR_HAND_JOINT_INDEX_METACARPAL_EXT].first.pose.orientation.z);

	// Get relative positions and orientations
	const auto rel_tip_x = glm::rotate(glm::conjugate(distal_q), tip_x - distal_x);
	// relative tip orientation is always identity

	const auto rel_distal_x = glm::rotate(glm::conjugate(intermediate_q), distal_x - intermediate_x);
	const auto rel_distal_q = glm::conjugate(intermediate_q) * distal_q;
	const auto rel_distal_θ = glm::eulerAngles(rel_distal_q);

	const auto rel_intermediate_x = glm::rotate(glm::conjugate(proximal_q), intermediate_x - proximal_x);
	const auto rel_intermediate_q = glm::conjugate(proximal_q) * intermediate_q;
	const auto rel_intermediate_θ = glm::eulerAngles(rel_intermediate_q);

	const auto rel_proximal_x = glm::rotate(glm::conjugate(metacarpal_q), proximal_x - metacarpal_x);
	const auto rel_proximal_q = glm::conjugate(metacarpal_q) * proximal_q;
	const auto rel_proximal_θ = glm::eulerAngles(rel_proximal_q);

	// Forward kinematics
	auto f = [&](float proximal_θx, float proximal_θy, float intermediate_θx, float distal_θx) -> glm::vec3 {
		// Compute the absolute tip position
		const glm::quat new_rel_proximal_q = glm::quat(glm::vec3(proximal_θx, proximal_θy, rel_proximal_θ.z));
		const glm::quat new_rel_intermediate_q = glm::quat(glm::vec3(intermediate_θx, rel_intermediate_θ.y, rel_intermediate_θ.z));
		const glm::quat new_rel_distal_q = glm::quat(glm::vec3(distal_θx, rel_distal_θ.y, rel_distal_θ.z));

		const glm::quat proximal_q = metacarpal_q * new_rel_proximal_q;
		const glm::quat intermediate_q = proximal_q * new_rel_intermediate_q;
		const glm::quat distal_q = intermediate_q * new_rel_distal_q;

		const glm::vec3 tip_x = metacarpal_x +
		                        glm::rotate(metacarpal_q, rel_proximal_x) +
		                        glm::rotate(proximal_q, rel_intermediate_x) +
		                        glm::rotate(intermediate_q, rel_distal_x) +
		                        glm::rotate(distal_q, rel_tip_x);

		return tip_x - target_position;
	};

	// Inverse kinematics solver for the tip position
	float proximal_θx = rel_proximal_θ.x;
	float proximal_θy = rel_proximal_θ.y;
	float intermediate_θx = rel_intermediate_θ.x;
	float distal_θx = rel_distal_θ.x;

	glm::vec3 objective = f(proximal_θx, proximal_θy, intermediate_θx, distal_θx);
	float eps = 0.01; // 10 milliradian ~ 0.57 deg
	int n = 0;

	while (glm::length(objective) > 1e-4 and n++ < 1000)
	{
		glm::vec3 dobj_1 = (f(proximal_θx + eps, proximal_θy, intermediate_θx, distal_θx) - objective) / eps;
		glm::vec3 dobj_2 = (f(proximal_θx, proximal_θy + eps, intermediate_θx, distal_θx) - objective) / eps;
		glm::vec3 dobj_3 = (f(proximal_θx, proximal_θy, intermediate_θx + eps, distal_θx) - objective) / eps;
		glm::vec3 dobj_4 = (f(proximal_θx, proximal_θy, intermediate_θx, distal_θx + eps) - objective) / eps;

		glm::mat4x3 jacobian(dobj_1, dobj_2, dobj_3, dobj_4);
		glm::mat3x4 jacobian_transpose = glm::transpose(jacobian);

		glm::vec4 direction = -glm::normalize(jacobian_transpose * objective);
		glm::vec4 delta = 10 * direction * glm::length(objective);

		float tmp = (delta.z + delta.w) / 2;
		delta.z = delta.w = tmp;

		proximal_θx = std::clamp<float>(proximal_θx + delta.x, -M_PI / 2, M_PI / 6);
		proximal_θy = std::clamp<float>(proximal_θy + delta.y, -M_PI / 6, M_PI / 6);
		intermediate_θx = std::clamp<float>(intermediate_θx + delta.z, -M_PI / 2, 0);
		distal_θx = std::clamp<float>(distal_θx + delta.w, -M_PI / 2, 0);

		objective = f(proximal_θx, proximal_θy, intermediate_θx, distal_θx);
	}

	// Fill in the modified bones
	const glm::vec3 new_rel_proximal_θ(proximal_θx, proximal_θy, rel_proximal_θ.z);
	const glm::vec3 new_rel_intermediate_θ(intermediate_θx, rel_intermediate_θ.y, rel_intermediate_θ.z);
	const glm::vec3 new_rel_distal_θ(distal_θx, rel_distal_θ.y, rel_distal_θ.z);

	const glm::quat new_rel_proximal_q = glm::quat(new_rel_proximal_θ);
	const glm::quat new_rel_intermediate_q = glm::quat(new_rel_intermediate_θ);
	const glm::quat new_rel_distal_q = glm::quat(new_rel_distal_θ);

	proximal_q = metacarpal_q * new_rel_proximal_q;
	intermediate_q = proximal_q * new_rel_intermediate_q;
	distal_q = intermediate_q * new_rel_distal_q;

	intermediate_x = proximal_x + glm::rotate(proximal_q, rel_intermediate_x);
	distal_x = intermediate_x + glm::rotate(intermediate_q, rel_distal_x);
	tip_x = distal_x + glm::rotate(distal_q, rel_tip_x);

	hand[XR_HAND_JOINT_INDEX_PROXIMAL_EXT].first.pose.orientation = {
	        proximal_q.x,
	        proximal_q.y,
	        proximal_q.z,
	        proximal_q.w,
	};
	hand[XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT].first.pose = {
	        .orientation = {
	                intermediate_q.x,
	                intermediate_q.y,
	                intermediate_q.z,
	                intermediate_q.w,
	        },
	        .position = {
	                intermediate_x.x,
	                intermediate_x.y,
	                intermediate_x.z,
	        },
	};
	hand[XR_HAND_JOINT_INDEX_DISTAL_EXT].first.pose = {
	        .orientation = {
	                distal_q.x,
	                distal_q.y,
	                distal_q.z,
	                distal_q.w,
	        },
	        .position = {
	                distal_x.x,
	                distal_x.y,
	                distal_x.z,
	        },
	};
	hand[XR_HAND_JOINT_INDEX_TIP_EXT].first.pose = {
	        .orientation = {
	                distal_q.x,
	                distal_q.y,
	                distal_q.z,
	                distal_q.w,
	        },
	        .position = {
	                tip_x.x,
	                tip_x.y,
	                tip_x.z,
	        },
	};

	// Translate the whole hand if we cannot find an exact solution
	for (auto & hand_joint: hand)
	{
		hand_joint.first.pose.position.x -= objective.x;
		hand_joint.first.pose.position.y -= objective.y;
		hand_joint.first.pose.position.z -= objective.z;
	}
}

void scenes::lobby::render(const XrFrameState & frame_state)
{
	if (async_session.valid() && async_session.poll() == utils::future_status::ready)
	{
		try
		{
			auto session = async_session.get();
			if (session)
				next_scene = stream::create(std::move(session), 1'000'000'000.f / frame_state.predictedDisplayPeriod, server_name, *this);

			async_session.reset();
		}
		catch (std::exception & e)
		{
			spdlog::error("Error connecting to server: {}", e.what());
			async_session.cancel();
			async_error = e.what();
		}
	}

	if (next_scene)
	{
		if (!next_scene->alive())
			next_scene.reset();
		else if (next_scene->current_state() == scenes::stream::state::streaming)
			application::push_scene(next_scene);
	}

	update_server_list();

	imgui_ctx->set_current();
	if (!async_session.valid() && !next_scene && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
	{
		if (auto intent = application::get_intent())
		{
			pin_request.lock()->pin = intent->pin;
			connect(configuration::server_data{
			        .manual = true,
			        .service = *intent,
			});
		}
		else
		{
			const auto & servers = application::get_config().servers;
			for (auto && [cookie, data]: servers)
			{
				if (data.visible && (data.autoconnect || force_autoconnect) && data.compatible && autoconnect_enabled)
				{
					autoconnect_enabled = false;
					connect(data);
					break;
				}
			}
		}
	}

	if (not frame_state.shouldRender)
	{
		session.begin_frame();
		session.end_frame(frame_state.predictedDisplayTime, {});
		return;
	}

	session.begin_frame();
	renderer->debug_draw_clear();

#if WIVRN_FEATURE_RENDERDOC
	renderdoc_begin(*vk_instance);
#endif

	XrSpace world_space = application::space(xr::spaces::world);
	auto [flags, views] = session.locate_views(viewconfig, frame_state.predictedDisplayTime, world_space);
	// assert(views.size() == swapchains_lobby.size());
	assert(views.size() == 2); // FIXME

	bool hide_left_controller = false;
	bool hide_right_controller = false;

	std::optional<std::pair<glm::vec3, glm::quat>> head_position = application::locate_controller(application::space(xr::spaces::view), world_space, frame_state.predictedDisplayTime);
	std::optional<glm::vec3> new_gui_position;

	if (head_position)
		new_gui_position = check_recenter_gui(head_position->first, head_position->second);

	if (!new_gui_position and head_position)
		new_gui_position = check_recenter_action(frame_state.predictedDisplayTime, head_position->first);

	if (left_hand and right_hand)
	{
		auto windows = imgui_ctx->windows();

		auto left = left_hand->locate(world_space, frame_state.predictedDisplayTime);
		if (left)
		{
			stick_finger_to_gui(*left, windows);
			hide_left_controller = true;
		}

		auto right = right_hand->locate(world_space, frame_state.predictedDisplayTime);
		if (right)
		{
			stick_finger_to_gui(*right, windows);
			hide_right_controller = true;
		}

		hand_model::apply(world, left, right);

		if (not new_gui_position and head_position)
			new_gui_position = check_recenter_gesture(xr::spaces::palm_left, left, *head_position);

		if (not new_gui_position and head_position)
			new_gui_position = check_recenter_gesture(xr::spaces::palm_right, right, *head_position);
	}

	if (head_position and new_gui_position)
	{
		move_gui(head_position->first, *new_gui_position);
	}

#if WIVRN_CLIENT_DEBUG_MENU
	{
		auto & left_node = world.get<components::node>(xyz_axes_left_controller);
		auto & right_node = world.get<components::node>(xyz_axes_right_controller);
		if (display_debug_axes)
		{
			const xr::spaces left = display_grip_instead_of_aim ? xr::spaces::grip_left : xr::spaces::aim_left;

			if (hide_left_controller)
				left_node.visible = false;
			else if (auto location = application::locate_controller(application::space(left), world_space, frame_state.predictedDisplayTime))
			{
				left_node.visible = true;
				left_node.position = location->first;
				left_node.orientation = location->second;
			}
			else
				left_node.visible = false;

			const xr::spaces right = display_grip_instead_of_aim ? xr::spaces::grip_right : xr::spaces::aim_right;
			if (hide_right_controller)
				right_node.visible = false;
			else if (auto location = application::locate_controller(application::space(right), world_space, frame_state.predictedDisplayTime))
			{
				right_node.visible = true;
				right_node.position = location->first;
				right_node.orientation = location->second;
			}
			else
				right_node.visible = false;
		}
		else
		{
			left_node.visible = false;
			right_node.visible = false;
		}
	}
#endif

	std::vector<std::pair<int, XrCompositionLayerQuad>> imgui_layers = draw_gui(frame_state.predictedDisplayTime);

#if WIVRN_CLIENT_DEBUG_MENU
	if (auto * node = world.try_get<components::node>(debug_primitive_to_highlight.first);
	    node and
	    node->mesh and
	    debug_primitive_to_highlight.second < node->mesh->primitives.size())
	{
		const auto & primitive = node->mesh->primitives[debug_primitive_to_highlight.second];
		// FIXME: transform_to_root is 1 frame late
		renderer->debug_draw_box(node->transform_to_root, primitive.obb_min, primitive.obb_max, glm::vec4(1, 1, 1, 1));
	}
#endif

	// Get the planes that limit the ray size from the composition layers
	std::vector<glm::mat4> world_to_window;
	for (auto & window: imgui_ctx->windows())
	{
		if (window.space == xr::spaces::world)
			world_to_window.push_back(glm::inverse(glm::translate(window.position) * glm::mat4(glm::mat3_cast(window.orientation)) * glm::scale(glm::vec3(window.size, 1))));
	}

	input->apply(world,
	             world_space,
	             frame_state.predictedDisplayTime,
	             hide_left_controller,
	             not imgui_ctx->is_aim_interaction()[0],
	             hide_right_controller,
	             not imgui_ctx->is_aim_interaction()[1],
	             world_to_window);

	renderer::animate(world, frame_state.predictedDisplayPeriod * 1.0e-9);

	world.get<components::node>(lobby_entity).visible = not application::get_config().passthrough_enabled;

	render_start(application::get_config().passthrough_enabled, frame_state.predictedDisplayTime);

	const XrColor4f clear_color = application::get_config().passthrough_enabled ? XrColor4f{0, 0, 0, 0} : constants::lobby::sky_color;
	render_world(
	        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
	        world_space,
	        views,
	        width,
	        height,
	        composition_layer_depth_test_supported,
	        composition_layer_depth_test_supported ? layer_lobby | layer_controllers : layer_lobby,
	        clear_color,
	        foveation,
	        true);

	if (composition_layer_depth_test_supported)
		set_depth_test(true, XR_COMPARE_OP_ALWAYS_FB);

	bool dim_gui = imgui_ctx->is_modal_popup_shown() and composition_layer_color_scale_bias_supported;
	for (auto & [z_index, layer]: imgui_layers)
	{
		if (z_index < constants::lobby::zindex_recenter_tip)
		{
			add_quad_layer(layer.layerFlags, layer.space, layer.eyeVisibility, layer.subImage, layer.pose, layer.size);

			if (dim_gui)
				set_color_scale_bias(constants::lobby::dimming_scale, constants::lobby::dimming_bias);

			if (composition_layer_depth_test_supported)
				set_depth_test(true, XR_COMPARE_OP_LESS_OR_EQUAL_FB);

			dim_gui = false; // Only dim the main window
		}
	}

	render_world(
	        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
	        world_space,
	        views,
	        width,
	        height,
	        composition_layer_depth_test_supported,
	        composition_layer_depth_test_supported ? layer_rays : layer_rays | layer_controllers,
	        {0, 0, 0, 0},
	        foveation);

	if (composition_layer_depth_test_supported)
		set_depth_test(true, XR_COMPARE_OP_LESS_OR_EQUAL_FB);

	for (auto & [z_index, layer]: imgui_layers)
	{
		if (z_index == constants::lobby::zindex_recenter_tip)
			add_quad_layer(layer.layerFlags, layer.space, layer.eyeVisibility, layer.subImage, layer.pose, layer.size);
	}

	render_end();

#if WIVRN_FEATURE_RENDERDOC
	renderdoc_end(*vk_instance);
#endif
}

void scenes::lobby::on_focused()
{
	recenter_gui = true;

	auto views = system.view_configuration_views(viewconfig);
	assert(views.size() == 2); // FIXME
	stream_view = override_view(views[0], guess_model());
	width = views[0].recommendedImageRectWidth;
	height = views[0].recommendedImageRectHeight;

	// assert(std::ranges::all_of(views, [width](const XrViewConfigurationView & view) { return view.recommendedImageRectWidth == width; }));
	// assert(std::ranges::all_of(views, [height](const XrViewConfigurationView & view) { return view.recommendedImageRectHeight == height; }));

	auto & config = application::get_config();

	try
	{
		lobby_entity = add_gltf(config.environment_model, layer_lobby).first;
	}
	catch (std::exception & e)
	{
		spdlog::warn("Cannot load environment from {}: {}, reverting to default", config.environment_model, e.what());
		config.environment_model = configuration{}.environment_model;
		lobby_entity = add_gltf(config.environment_model, layer_lobby).first;
		config.save();
	}

	std::string profile = controller_name();
	input.emplace(
	        *this,
	        "assets://controllers/" + profile + "/profile.json",
	        layer_controllers,
	        layer_rays,
	        get_action("left_trigger").first,
	        get_action("right_trigger").first);

	spdlog::info("Loaded input profile {}", input->id);

	for (auto i: {xr::spaces::aim_left, xr::spaces::aim_right, xr::spaces::grip_left, xr::spaces::grip_right})
	{
		auto [p, q] = input->offset[i] = controller_offset(controller_name(), i);

		auto rot = glm::degrees(glm::eulerAngles(q));
		spdlog::info("Initializing offset of space {} to ({}, {}, {}) mm, ({}, {}, {})°",
		             magic_enum::enum_name(i),
		             1000 * p.x,
		             1000 * p.y,
		             1000 * p.z,
		             rot.x,
		             rot.y,
		             rot.z);
	}

#if WIVRN_CLIENT_DEBUG_MENU
	offset_position = input->offset[xr::spaces::grip_left].first;
	offset_orientation = glm::degrees(glm::eulerAngles(input->offset[xr::spaces::grip_left].second));
	ray_offset = input->offset[xr::spaces::aim_left].first.z;

	xyz_axes_left_controller = add_gltf("assets://xyz-arrows.glb", layer_controllers).first;
	xyz_axes_right_controller = add_gltf("assets://xyz-arrows.glb", layer_controllers).first;
#endif

	recenter_left_action = get_action("recenter_left").first;
	recenter_right_action = get_action("recenter_right").first;

	if (system.hand_tracking_supported())
	{
		left_hand = session.create_hand_tracker(XR_HAND_LEFT_EXT);
		right_hand = session.create_hand_tracker(XR_HAND_RIGHT_EXT);
		hand_model::add_hand(*this, XR_HAND_LEFT_EXT, "assets://left-hand.glb", layer_controllers);
		hand_model::add_hand(*this, XR_HAND_RIGHT_EXT, "assets://right-hand.glb", layer_controllers);
	}

	std::vector imgui_inputs{
	        imgui_context::controller{
	                .aim = get_action_space("left_aim"),
	                .offset = input->offset[xr::spaces::aim_left],
	                .trigger = get_action("left_trigger").first,
	                .squeeze = get_action("left_squeeze").first,
	                .scroll = get_action("left_scroll").first,
	                .haptic_output = get_action("left_haptic").first,
	                .hand = left_hand ? &*left_hand : nullptr,
	        },
	        imgui_context::controller{
	                .aim = get_action_space("right_aim"),
	                .offset = input->offset[xr::spaces::aim_right],
	                .trigger = get_action("right_trigger").first,
	                .squeeze = get_action("right_squeeze").first,
	                .scroll = get_action("right_scroll").first,
	                .haptic_output = get_action("right_haptic").first,
	                .hand = right_hand ? &*right_hand : nullptr,
	        },
	};

	// 0.4mm / pixel
	std::vector<imgui_context::viewport> vps{
	        {
	                // Main window
	                .space = xr::spaces::world,
	                .size = {0.6, 0.4},
	                .vp_origin = {0, 0},
	                .vp_size = {1500, 1000},
	                .z_index = constants::lobby::zindex_gui,
	        },
	        {
	                // Pop up window
	                .space = xr::spaces::world,
	                .size = {0.6, 0.4},
	                .vp_origin = {1500, 0},
	                .vp_size = {1500, 1000},
	                .z_index = constants::lobby::zindex_gui,
	        },
	        {
	                // Virtual keyboard
	                .space = xr::spaces::world,
	                .size = {0.6, 0.2},
	                .vp_origin = {1500, 1000},
	                .vp_size = {1500, 500},
	                .always_show_cursor = true,
	                .z_index = constants::lobby::zindex_gui,
	        },
	        {
	                // Recenter tip
	                .space = xr::spaces::view,
	                .position = {0, -0.4, -1.0},
	                .orientation = {1, 0, 0, 0},
	                .size = {0.6, 0.12},
	                .vp_origin = {0, 1000},
	                .vp_size = {1500, 300},
	                .z_index = constants::lobby::zindex_recenter_tip,
	        },
	        {
	                // Tooltip
	                .space = xr::spaces::world,
	                .size = {0.6, 0.08},
	                .vp_origin = {0, 1300},
	                .vp_size = {1500, 200},
	                .tooltip_viewport = true,
	                .z_index = constants::lobby::zindex_tooltip,
	        },
	};

	xr::swapchain swapchain_imgui(instance, session, device, swapchain_format, 3000, 1500);

	imgui_ctx.emplace(
	        physical_device,
	        device,
	        queue_family_index,
	        queue,
	        imgui_inputs,
	        std::move(swapchain_imgui),
	        vps,
	        image_cache);

	supported_codecs = wivrn::decoder::supported_codecs();

	auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	auto tm = std::localtime(&t);
	switch (tm->tm_mon)
	{
		case 5:
			about_picture = imgui_ctx->load_texture("assets://wivrn-pride.ktx2");
			break;
		case 11:
			about_picture = imgui_ctx->load_texture("assets://wivrn-christmas.ktx2");
			break;
		default:
			about_picture = imgui_ctx->load_texture("assets://wivrn.ktx2");
			break;
	}

	default_environment_screenshot = imgui_ctx->load_texture("assets://default-environment.ktx2");

	try
	{
		local_environments = load_environment_json(utils::read_whole_file<std::string>(application::get_config_path() / "environments.json"));

		// Remove environments if the model file is deleted
		std::erase_if(local_environments, [&](const environment_model & model) {
			std::filesystem::path path = model.local_gltf_path;
			return not std::filesystem::exists(path);
		});
	}
	catch (...)
	{
	}

	local_environments.push_back(
	        environment_model{
	                .name = gettext_noop("Passthrough"),
	                .author = "",
	                .description = "",
	                .screenshot_url = "",
	                .gltf_url = "passthrough", // This needs to be unique because it is used as a key, even if there is no actual URL
	                .builtin = true,
	                .override_order = -2,
	                .local_gltf_path = "",
	                .screenshot = imgui_ctx->load_texture("assets://passthrough.ktx2")});

	local_environments.push_back(
	        environment_model{
	                .name = gettext_noop("Default environment"),
	                .author = "",
	                .description = "",
	                .screenshot_url = "",
	                .gltf_url = "default",
	                .builtin = true,
	                .override_order = -1,
	                .local_gltf_path = configuration{}.environment_model,
	                .screenshot = imgui_ctx->load_texture("assets://default-environment.ktx2")});

	std::ranges::sort(local_environments, std::less{});

	setup_passthrough();
	multicast = application::get_wifi_lock().get_multicast_lock();

	session.set_performance_level(XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
	session.set_performance_level(XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
}

void scenes::lobby::setup_passthrough()
{
	if (application::get_config().passthrough_enabled)
		session.enable_passthrough(system);
	else
		session.disable_passthrough();
}

void scenes::lobby::on_unfocused()
{
	discover.reset();

	renderer->wait_idle(); // Must be before the scene data because the renderer uses its descriptor sets

	about_picture = 0;
	default_environment_screenshot = 0;
	local_environments.clear();

	imgui_ctx.reset();
	world = entt::registry{};

	input.reset();
	left_hand.reset();
	right_hand.reset();
	face_tracker.emplace<std::monostate>();

	clear_swapchains();
	multicast.reset();
}

void scenes::lobby::on_xr_event(const xr::event & event)
{
	switch (event.header.type)
	{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			if (event.state_changed.state == XR_SESSION_STATE_STOPPING)
				discover.reset();
			else if (event.state_changed.state == XR_SESSION_STATE_FOCUSED)
				autoconnect_enabled = true;
			recenter_gui = true;
			break;
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			recenter_gui = true;
			break;
		default:
			break;
	}
	if (next_scene)
		next_scene->on_xr_event(event);
}

scene::meta & scenes::lobby::get_meta_scene()
{
	static meta m{
	        .name = "Lobby",
	        .actions = {
	                {"left_aim", XR_ACTION_TYPE_POSE_INPUT},
	                {"left_trigger", XR_ACTION_TYPE_FLOAT_INPUT},
	                {"left_squeeze", XR_ACTION_TYPE_FLOAT_INPUT},
	                {"left_scroll", XR_ACTION_TYPE_VECTOR2F_INPUT},
	                {"left_haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT},
	                {"right_aim", XR_ACTION_TYPE_POSE_INPUT},
	                {"right_trigger", XR_ACTION_TYPE_FLOAT_INPUT},
	                {"right_squeeze", XR_ACTION_TYPE_FLOAT_INPUT},
	                {"right_scroll", XR_ACTION_TYPE_VECTOR2F_INPUT},
	                {"right_haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT},

	                {"recenter_left", XR_ACTION_TYPE_BOOLEAN_INPUT},
	                {"recenter_right", XR_ACTION_TYPE_BOOLEAN_INPUT},
	        },
	        .bindings = {
	                suggested_binding{
	                        {
	                                "/interaction_profiles/oculus/touch_controller",
	                                "/interaction_profiles/facebook/touch_controller_pro",
	                                "/interaction_profiles/meta/touch_pro_controller",
	                                "/interaction_profiles/meta/touch_controller_plus",
	                                "/interaction_profiles/meta/touch_plus_controller",
	                                "/interaction_profiles/bytedance/pico_neo3_controller",
	                                "/interaction_profiles/bytedance/pico4_controller",
	                                "/interaction_profiles/bytedance/pico4s_controller",
	                                "/interaction_profiles/htc/vive_focus3_controller",
	                        },
	                        {
	                                {"left_aim", "/user/hand/left/input/aim/pose"},
	                                {"left_trigger", "/user/hand/left/input/trigger/value"},
	                                {"left_squeeze", "/user/hand/left/input/squeeze/value"},
	                                {"left_scroll", "/user/hand/left/input/thumbstick"},
	                                {"left_haptic", "/user/hand/left/output/haptic"},
	                                {"right_aim", "/user/hand/right/input/aim/pose"},
	                                {"right_trigger", "/user/hand/right/input/trigger/value"},
	                                {"right_squeeze", "/user/hand/right/input/squeeze/value"},
	                                {"right_scroll", "/user/hand/right/input/thumbstick"},
	                                {"right_haptic", "/user/hand/right/output/haptic"},

	                                {"recenter_left", "/user/hand/left/input/squeeze/value"},
	                                {"recenter_right", "/user/hand/right/input/squeeze/value"},
	                        },
	                },
	                suggested_binding{
	                        {
	                                "/interaction_profiles/khr/simple_controller",
	                        },
	                        {
	                                {"left_aim", "/user/hand/left/input/aim/pose"},
	                                {"left_trigger", "/user/hand/left/input/select/click"},
	                                {"left_squeeze", "/user/hand/left/input/menu/click"},
	                                {"right_aim", "/user/hand/right/input/aim/pose"},
	                                {"right_trigger", "/user/hand/right/input/select/click"},
	                                {"right_squeeze", "/user/hand/right/input/menu/click"},
	                        },
	                },
	                suggested_binding{
	                        {
	                                "/interaction_profiles/ext/hand_interaction_ext",
	                        },
	                        {
	                                {"left_aim", "/user/hand/left/input/aim/pose"},
	                                {"left_trigger", "/user/hand/left/input/aim_activate_ext"},
	                                {"right_aim", "/user/hand/right/input/aim/pose"},
	                                {"right_trigger", "/user/hand/right/input/aim_activate_ext"},
	                        },
	                },
	        }};

	return m;
}
