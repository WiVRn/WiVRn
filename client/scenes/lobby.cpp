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
#include "render/scene_renderer.h"
#include "stream.h"
#include "hardware.h"
#include "wivrn_client.h"
#include <chrono>
#include <future>
#include <glm/gtc/matrix_access.hpp>

#include "wivrn_discover.h"
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <ios>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <utils/ranges.h>
#include <vulkan/vulkan_raii.hpp>
#include <simdjson.h>
#include <fstream>
#include <thread>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

using namespace std::chrono_literals;

static const std::string discover_service = "_wivrn._tcp.local.";
static bool force_autoconnect = false;

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
	const float gui_target_distance = 0.75;
	const float gui_target_altitude = 1.;
	const float gui_target_pitch = -0.5;

	glm::vec3 gui_direction = glm::column(glm::mat3_cast(imgui_ctx->orientation()), 2);
	float gui_yaw = atan2(gui_direction.x, gui_direction.z);

	glm::vec3 head_direction = -glm::column(glm::mat3_cast(orientation), 2);
	float head_yaw = atan2(head_direction.x, head_direction.z) + M_PI;

	head_direction.y = 0;
	head_direction = glm::normalize(head_direction);
	position.y = gui_target_altitude;

	glm::vec3 gui_target_position = position + gui_target_distance * head_direction;

	glm::vec3 gui_position_error = gui_target_position - imgui_ctx->position();
	float gui_yaw_error = remainderf(head_yaw - gui_yaw, 2 * M_PI);

	if (move_gui_first_time)
	{
		move_gui_first_time = false;
		gui_yaw += gui_yaw_error;

		imgui_ctx->position() += gui_position_error;
		imgui_ctx->orientation() = glm::quat(cos(gui_yaw/2), 0, sin(gui_yaw/2), 0) * glm::quat(cos(gui_target_pitch/2), sin(gui_target_pitch/2), 0, 0);
	}
}

scenes::lobby::lobby()
{
	if (std::getenv("WIVRN_AUTOCONNECT"))
		force_autoconnect = true;

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
		if (server_data.autoconnect || server_data.manual)
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
	if (!manual_connection)
	{
		char protocol_string[17];
		sprintf(protocol_string, "%016lx", xrt::drivers::wivrn::protocol_version);

		spdlog::debug("Client protocol version: {}", protocol_string);
		spdlog::debug("Server TXT:");
		for(auto& [i,j]: service.txt)
		{
			spdlog::debug("    {}=\"{}\"", i, j);
		}

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
			spdlog::error("Cannot resolve hostname {}: {}", service.hostname, gai_strerror(err));
			throw std::runtime_error("Cannot resolve hostname: " + std::string(gai_strerror(err)));
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
		std::string address_string = std::visit([](auto & address) {
			return ip_address_to_string(address);
		}, address);

		try
		{
			spdlog::debug("Trying address {}", address_string);

			return std::visit([port = service.port](auto & address) {
				return std::make_unique<wivrn_session>(address, port);
			},
			address);
		}
		catch (std::exception & e)
		{
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
		if (service.txt.find("cookie") == service.txt.end())
		{
			spdlog::info("Ignored {} because there is no cookie field", service.name);
			continue;
		}

		auto cookie = service.txt.at("cookie");
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

void scenes::lobby::connect(server_data& data)
{
	server_name = data.service.name;
	async_error.reset();

	async_session = utils::async<std::unique_ptr<wivrn_session>, std::string>([](auto token, wivrn_discover::service service, bool manual)
	{
		token.set_progress("Waiting for connection");
		return connect_to_session(service, manual);
	}, data.service, data.manual);
}

static std::vector<XrCompositionLayerProjectionView> render_layer(std::vector<XrView>& views, std::vector<xr::swapchain>& swapchains, scene_renderer& renderer, scene_data& data, const std::array<float, 4>& clear_color)
{
	std::vector<scene_renderer::frame_info> frames;
	frames.reserve(views.size());

	std::vector<XrCompositionLayerProjectionView> layer_views;
	layer_views.reserve(views.size());

	for (auto && [view, swapchain]: utils::zip(views, swapchains))
	{
		int image_index = swapchain.acquire();
		swapchain.wait();

		frames.push_back({.destination = swapchain.images()[image_index].image,
				.projection = projection_matrix(view.fov),
				.view = view_matrix(view.pose)});

		layer_views.push_back({
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

	renderer.render(data, clear_color, frames);

	return layer_views;
}

void scenes::lobby::render()
{
	if (async_session.valid() && async_session.poll() == utils::future_status::ready)
	{
		try
		{
			auto session = async_session.get();
			if (session)
				next_scene = stream::create(std::move(session));

			async_session.reset();
		}
		catch(std::exception& e)
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
		else if (next_scene->ready())
			application::push_scene(next_scene);
	}

	update_server_list();

	if (!async_session.valid() && !next_scene && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
	{
		for(auto&& [cookie, data]: servers)
		{
			if (data.visible && (data.autoconnect || force_autoconnect))
			{
				connect(data);
			}
		}
	}

	XrFrameState framestate = session.wait_frame();

	if (!framestate.shouldRender)
	{
		spdlog::debug("framestate.shouldRender is false");
		session.begin_frame();
		session.end_frame(framestate.predictedDisplayTime, {});
		return;
	}

	session.begin_frame();

	auto [flags, views] = session.locate_views(viewconfig, framestate.predictedDisplayTime, world_space);
	assert(views.size() == swapchains_lobby.size());

	input->apply(world_space, framestate.predictedDisplayTime);

	XrCompositionLayerQuad imgui_layer = draw_gui(framestate.predictedDisplayTime);

	assert(renderer);
	renderer->start_frame();
	auto lobby_layer_views = render_layer(views, swapchains_lobby, *renderer, *lobby_scene, {0, 0.25, 0.5, 1});
	auto controllers_layer_views = render_layer(views, swapchains_controllers, *renderer, *controllers_scene, {0, 0, 0, 0});
	renderer->end_frame();

	// After end_frame because the command buffers are submitted in end_frame
	for (auto & swapchain: swapchains_lobby)
		swapchain.release();

	for (auto & swapchain: swapchains_controllers)
		swapchain.release();

	XrCompositionLayerProjection lobby_layer{
	        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	        .layerFlags = 0,
	        .space = world_space,
	        .viewCount = (uint32_t)lobby_layer_views.size(),
	        .views = lobby_layer_views.data(),
	};

	XrCompositionLayerProjection controllers_layer{
	        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
	        .space = world_space,
	        .viewCount = (uint32_t)controllers_layer_views.size(),
	        .views = controllers_layer_views.data(),
	};

	std::vector<XrCompositionLayerBaseHeader *> layers_base;
	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&lobby_layer));
	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&imgui_layer));
	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&controllers_layer));

	session.end_frame(framestate.predictedDisplayTime, layers_base);
}

void scenes::lobby::on_focused()
{
	discover.emplace(discover_service);
	move_gui_first_time = true;

	auto views = system.view_configuration_views(viewconfig);
	uint32_t width = views[0].recommendedImageRectWidth;
	uint32_t height = views[0].recommendedImageRectHeight;

	swapchains_lobby.reserve(views.size());
	swapchains_controllers.reserve(views.size());
	for ([[maybe_unused]] auto view: views)
	{
		assert(view.recommendedImageRectWidth == width);
		assert(view.recommendedImageRectHeight == height);

		swapchains_lobby.emplace_back(session, device, swapchain_format, width, height);
		swapchains_controllers.emplace_back(session, device, swapchain_format, width, height);
	}

	spdlog::info("Created lobby swapchains: {}x{}", width, height);

	vk::Extent2D output_size{width, height};

	std::array depth_formats{
	        vk::Format::eX8D24UnormPack32,
	        vk::Format::eD32Sfloat,
	};

	renderer.emplace(device, physical_device, queue, commandpool, output_size, swapchain_format, depth_formats);

	scene_loader loader(device, physical_device, queue, application::queue_family_index(), renderer->get_default_material());

	lobby_scene.emplace();
	lobby_scene->import(loader("ground.gltf"));

	controllers_scene.emplace();
	input = input_profile("controllers/" + choose_webxr_profile() + "/profile.json", loader, *controllers_scene);
	spdlog::info("Loaded input profile {}", input->id);

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

	swapchain_imgui = xr::swapchain(session, device, swapchain_format, 1500, 1000);
	imgui_ctx.emplace(physical_device, device, queue_family_index, queue, world_space, imgui_inputs, swapchain_imgui, glm::vec2{1.0, 0.6666});

	try
	{
		about_picture = imgui_ctx->load_texture("wivrn.ktx2");
	}
	catch(...)
	{
		about_picture = imgui_ctx->load_texture("wivrn.png");
	}
}

void scenes::lobby::on_unfocused()
{
	discover.reset();

	renderer->wait_idle(); // Must be before the scene data because the renderer uses its descriptor sets

	about_picture = nullptr;
	imgui_ctx.reset();
	lobby_scene.reset(); // Must be reset before the renderer so that the descriptor sets are freed before their pools
	controllers_scene.reset();
	renderer.reset();
	swapchains_lobby.clear();
	swapchains_controllers.clear();
	swapchain_imgui.reset();
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
