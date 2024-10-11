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
#include "glm/geometric.hpp"
#include "hardware.h"
#include "imgui.h"
#include "input_profile.h"
#include "openxr/openxr.h"
#include "render/scene_data.h"
#include "render/scene_renderer.h"
#include "stream.h"
#include "utils/contains.h"
#include "version.h"
#include "wivrn_client.h"
#include "xr/passthrough.h"
#include <glm/gtc/matrix_access.hpp>

#include "wivrn_discover.h"
#include <cstdint>
#include <glm/gtc/quaternion.hpp>
#include <magic_enum.hpp>
#include <simdjson.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utils/ranges.h>
#include <vulkan/vulkan_raii.hpp>
#include <ranges>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

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

	switch (guess_model())
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
		case model::lynx_r1:
		case model::unknown:
			return "generic-trigger-squeeze";
	}

	__builtin_unreachable();
}

static const std::array supported_formats = {
        vk::Format::eR8G8B8A8Srgb,
        vk::Format::eB8G8R8A8Srgb,
};

void scenes::lobby::move_gui(glm::vec3 head_position, glm::vec3 new_gui_position)
{
	const float gui_pitch = -0.2;
	glm::vec3 gui_direction = new_gui_position - head_position;

	float gui_yaw = atan2(gui_direction.x, gui_direction.z) + M_PI;

	imgui_ctx->position() = new_gui_position;
	imgui_ctx->orientation() = glm::quat(cos(gui_yaw / 2), 0, sin(gui_yaw / 2), 0) * glm::quat(cos(gui_pitch / 2), sin(gui_pitch / 2), 0, 0);
}

scenes::lobby::lobby()
{
	if (std::getenv("WIVRN_AUTOCONNECT"))
		force_autoconnect = true;

	passthrough_supported = system.passthrough_supported();

	auto & servers = application::get_config().servers;
	spdlog::info("{} known server(s):", servers.size());
	for (auto & i: servers)
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
		throw std::runtime_error(_("No supported swapchain format"));

	std::array depth_formats{
	        vk::Format::eX8D24UnormPack32,
	        vk::Format::eD32Sfloat,
	};

	auto views = system.view_configuration_views(viewconfig);
	stream_view = override_view(views[0], guess_model());
	uint32_t width = views[0].recommendedImageRectWidth;
	uint32_t height = views[0].recommendedImageRectHeight;

	depth_format = scene_renderer::find_usable_image_format(
	        physical_device,
	        depth_formats,
	        {
	                width,
	                height,
	                1,
	        },
	        vk::ImageUsageFlagBits::eDepthStencilAttachment);

	spdlog::info("Using formats {} and {}", vk::to_string(swapchain_format), vk::to_string(depth_format));

	composition_layer_depth_test_supported =
	        instance.has_extension(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) and
	        instance.has_extension(XR_FB_COMPOSITION_LAYER_DEPTH_TEST_EXTENSION_NAME);

	if (composition_layer_depth_test_supported)
		spdlog::info("Composition layer depth test supported");
	else
		spdlog::info("Composition layer depth test NOT supported");
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

std::unique_ptr<wivrn_session> connect_to_session(wivrn_discover::service service, bool manual_connection)
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
	for (const auto & address: service.addresses)
	{
		std::string address_string = std::visit([](auto & address) {
			return ip_address_to_string(address);
		},
		                                        address);

		try
		{
			spdlog::debug("Trying address {}", address_string);

			return std::visit([port = service.port, tcp_only = service.tcp_only](auto & address) {
				return std::make_unique<wivrn_session>(address, port, tcp_only);
			},
			                  address);
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
	        [](auto token, wivrn_discover::service service, bool manual) {
		        token.set_progress(_("Waiting for connection"));
		        return connect_to_session(service, manual);
	        },
	        data.service,
	        data.manual);
}

std::optional<glm::vec3> scenes::lobby::check_recenter_gesture(const std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT> & joints)
{
	const auto & palm = joints[XR_HAND_JOINT_PALM_EXT].first;
	const auto & o = palm.pose.orientation;
	const auto & p = palm.pose.position;
	glm::quat q{o.w, o.x, o.y, o.z};
	glm::vec3 v{p.x, p.y, p.z};

	if (glm::dot(q * glm::vec3(0, 1, 0), glm::vec3(0, -1, 0)) > 0.8)
	{
		return v + glm::vec3(0, 0.3, 0) + q * glm::vec3(0, 0, -0.2);
	}

	return std::nullopt;
}

std::optional<glm::vec3> scenes::lobby::check_recenter_action(XrTime predicted_display_time)
{
	std::optional<std::pair<glm::vec3, glm::quat>> aim;

	if (application::read_action_bool(recenter_left_action).value_or(std::pair{0, false}).second)
	{
		aim = application::locate_controller(application::space(xr::spaces::aim_left), application::space(xr::spaces::world), predicted_display_time);
	}

	if (application::read_action_bool(recenter_right_action).value_or(std::pair{0, false}).second)
	{
		aim = application::locate_controller(application::space(xr::spaces::aim_right), application::space(xr::spaces::world), predicted_display_time);
	}

	if (aim)
	{
		if (not gui_recenter_distance)
		{
			auto intersection = imgui_ctx->ray_plane_intersection({
			        .active = true,
			        .aim_position = aim->first,
			        .aim_orientation = aim->second,
			});
			if (intersection)
			{
				gui_recenter_distance = std::clamp(intersection->second, 0.05f, 1.f);
				spdlog::info("recentering at {}m", intersection->second);
			}
			else
			{
				gui_recenter_distance = 0.3;
			}
		}
		const auto & v = aim->first;
		const auto & q = aim->second;
		return v + q * glm::vec3(0, 0, -*gui_recenter_distance);
	}
	else
	{
		gui_recenter_distance.reset();
	}

	return std::nullopt;
}

std::optional<glm::vec3> scenes::lobby::check_recenter_gui(glm::vec3 head_position, glm::quat head_orientation)
{
	const float gui_distance = 0.50;

	glm::vec3 head_direction = -glm::column(glm::mat3_cast(head_orientation), 2);

	if (recenter_gui)
	{
		recenter_gui = false;
		glm::vec3 new_gui_position = head_position + gui_distance * head_direction;
		new_gui_position.y = head_position.y - 0.1;
		return new_gui_position;
	}

	return std::nullopt;
}

static std::vector<XrCompositionLayerProjectionView> render_layer(std::vector<XrView> & views, std::vector<xr::swapchain> & color_swapchains, std::vector<xr::swapchain> & depth_swapchains, scene_renderer & renderer, scene_data & data, const std::array<float, 4> & clear_color)
{
	std::vector<scene_renderer::frame_info> frames;
	frames.reserve(views.size());

	std::vector<XrCompositionLayerProjectionView> layer_views;
	layer_views.reserve(views.size());

	for (auto && [view, color_swapchain]: utils::zip(views, color_swapchains /*, depth_swpachains */))
	{
		int color_image_index = color_swapchain.acquire();
		color_swapchain.wait();

		frames.push_back({
		        .destination = color_swapchain.images()[color_image_index].image,
		        .projection = projection_matrix(view.fov),
		        .view = view_matrix(view.pose),
		});

		layer_views.push_back({
		        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
		        .pose = view.pose,
		        .fov = view.fov,
		        .subImage = {
		                .swapchain = color_swapchain,
		                .imageRect = {
		                        .offset = {0, 0},
		                        .extent = color_swapchain.extent(),
		                },
		        },
		});
	}

	renderer.render(data, clear_color, frames);

	return layer_views;
}

static std::vector<XrCompositionLayerProjectionView> render_layer(std::vector<XrView> & views, std::vector<xr::swapchain> & color_swapchains, scene_renderer & renderer, scene_data & data, const std::array<float, 4> & clear_color)
{
	std::vector<xr::swapchain> depth_swapchains;
	return render_layer(views, color_swapchains, depth_swapchains, renderer, data, clear_color);
}

namespace
{
template <class... Ts>
struct overloaded : Ts...
{
	using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
} // namespace

void scenes::lobby::render(const XrFrameState & frame_state)
{
	if (async_session.valid() && async_session.poll() == utils::future_status::ready)
	{
		try
		{
			auto session = async_session.get();
			if (session)
				next_scene = stream::create(std::move(session), 1'000'000'000.f / frame_state.predictedDisplayPeriod);

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
		{
			autoconnect_enabled = true;
			application::push_scene(next_scene);
		}
	}

	update_server_list();

	imgui_ctx->set_current();
	if (!async_session.valid() && !next_scene && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup))
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

	if (not frame_state.shouldRender)
	{
		session.begin_frame();
		session.end_frame(frame_state.predictedDisplayTime, {});
		return;
	}

	session.begin_frame();

	XrSpace world_space = application::space(xr::spaces::world);
	auto [flags, views] = session.locate_views(viewconfig, frame_state.predictedDisplayTime, world_space);
	assert(views.size() == swapchains_lobby.size());

	bool hide_left_controller = false;
	bool hide_right_controller = false;

	std::optional<std::pair<glm::vec3, glm::quat>> head_position = application::locate_controller(application::space(xr::spaces::view), world_space, frame_state.predictedDisplayTime);
	std::optional<glm::vec3> new_gui_position;

	if (head_position)
		new_gui_position = check_recenter_gui(head_position->first, head_position->second);

	if (!new_gui_position)
		new_gui_position = check_recenter_action(frame_state.predictedDisplayTime);

	if (application::get_hand_tracking_supported())
	{
		if (left_hand)
		{
			auto & hand = application::get_left_hand();
			auto joints = hand.locate(world_space, frame_state.predictedDisplayTime);
			left_hand->apply(joints);

			if (joints and xr::hand_tracker::check_flags(*joints, XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT, 0))
			{
				hide_left_controller = true;
				if (!new_gui_position)
					new_gui_position = check_recenter_gesture(*joints);
			}
		}

		if (right_hand)
		{
			auto & hand = application::get_right_hand();
			auto joints = hand.locate(world_space, frame_state.predictedDisplayTime);
			right_hand->apply(joints);

			if (joints and xr::hand_tracker::check_flags(*joints, XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT, 0))
			{
				hide_right_controller = true;
				if (!new_gui_position)
					new_gui_position = check_recenter_gesture(*joints);
			}
		}
	}

	input->apply(world_space, frame_state.predictedDisplayTime, hide_left_controller, hide_right_controller);

	if (head_position && new_gui_position)
	{
		move_gui(head_position->first, *new_gui_position);
	}

	XrCompositionLayerQuad imgui_layer = draw_gui(frame_state.predictedDisplayTime);

	assert(renderer);
	renderer->start_frame();
	if (composition_layer_depth_test_supported)
	{
		// TODO
		abort();
	}

	std::vector<XrCompositionLayerProjectionView> lobby_layer_views;
	if (not application::get_config().passthrough_enabled)
		lobby_layer_views = render_layer(views, swapchains_lobby, *renderer, *lobby_scene, {0, 0.25, 0.5, 1});

	auto controllers_layer_views = render_layer(views, swapchains_controllers, *renderer, *controllers_scene, {0, 0, 0, 0});
	renderer->end_frame();

	// After end_frame because the command buffers are submitted in end_frame
	if (not application::get_config().passthrough_enabled)
	{
		for (auto & swapchain: swapchains_lobby)
			swapchain.release();
	}

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

	XrEnvironmentBlendMode blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	std::vector<XrCompositionLayerBaseHeader *> layers_base;

	if (application::get_config().passthrough_enabled)
	{
		std::visit(
		        overloaded{
		                [&](std::monostate &) {
			                assert(false);
		                },
		                [&](xr::passthrough_alpha_blend & p) {
			                blend_mode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
		                },
		                [&](auto & p) {
			                layers_base.push_back(p.layer());
		                }},
		        passthrough);
	}
	else
	{
		layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&lobby_layer));
	}
	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&imgui_layer));
	layers_base.push_back(reinterpret_cast<XrCompositionLayerBaseHeader *>(&controllers_layer));

	session.end_frame(frame_state.predictedDisplayTime, layers_base, blend_mode);
}

void scenes::lobby::on_focused()
{
	recenter_gui = true;

	auto views = system.view_configuration_views(viewconfig);
	stream_view = override_view(views[0], guess_model());
	uint32_t width = views[0].recommendedImageRectWidth;
	uint32_t height = views[0].recommendedImageRectHeight;

	if (composition_layer_depth_test_supported)
	{
	}
	else
	{
	}

	swapchains_lobby.reserve(views.size());
	swapchains_controllers.reserve(views.size());
	swapchains_depth.reserve(views.size());
	for ([[maybe_unused]] auto view: views)
	{
		assert(view.recommendedImageRectWidth == width);
		assert(view.recommendedImageRectHeight == height);

		swapchains_lobby.emplace_back(session, device, swapchain_format, width, height);
		swapchains_controllers.emplace_back(session, device, swapchain_format, width, height);
	}

	spdlog::info("Created lobby swapchains: {}x{}", width, height);

	vk::Extent2D output_size{width, height};

	renderer.emplace(device, physical_device, queue, commandpool, output_size, swapchain_format, depth_format);

	scene_loader loader(device, physical_device, queue, application::queue_family_index(), renderer->get_default_material());

	lobby_scene.emplace();
	lobby_scene->import(loader("ground.gltf"));

	controllers_scene.emplace();
	input = input_profile("controllers/" + choose_webxr_profile() + "/profile.json", loader, *controllers_scene);
	spdlog::info("Loaded input profile {}", input->id);

	if (application::get_hand_tracking_supported())
	{
		left_hand.emplace("left-hand.glb", loader, *controllers_scene);
		right_hand.emplace("right-hand.glb", loader, *controllers_scene);
	}

	recenter_left_action = get_action("recenter_left").first;
	recenter_right_action = get_action("recenter_right").first;

	std::vector imgui_inputs{
	        imgui_context::controller{
	                .aim = get_action_space("left_aim"),
	                .trigger = get_action("left_trigger").first,
	                .squeeze = get_action("left_squeeze").first,
	                .scroll = get_action("left_scroll").first,
	        },
	        imgui_context::controller{
	                .aim = get_action_space("right_aim"),
	                .trigger = get_action("right_trigger").first,
	                .squeeze = get_action("right_squeeze").first,
	                .scroll = get_action("right_scroll").first,
	        },
	};
	if (auto & hand = application::get_left_hand())
	{
		imgui_inputs.push_back(
		        {
		                .hand = &hand,
		        });
	}
	if (auto & hand = application::get_right_hand())
	{
		imgui_inputs.push_back(
		        {
		                .hand = &hand,
		        });
	}

	swapchain_imgui = xr::swapchain(session, device, swapchain_format, 1500, 1000);
	imgui_ctx.emplace(physical_device, device, queue_family_index, queue, application::space(xr::spaces::world), imgui_inputs, swapchain_imgui, glm::vec2{0.6, 0.4});

	try
	{
		about_picture = imgui_ctx->load_texture("wivrn.ktx2");
	}
	catch (...)
	{
		about_picture = imgui_ctx->load_texture("wivrn.png");
	}
	setup_passthrough();
	multicast = application::get_wifi_lock().get_multicast_lock();
}

void scenes::lobby::setup_passthrough()
{
	auto & passthrough_enabled = application::get_config().passthrough_enabled;
	if (not passthrough_enabled)
	{
		passthrough.emplace<std::monostate>();
		return;
	}
	if (passthrough_supported != xr::system::passthrough_type::no_passthrough)
	{
		if (utils::contains(system.environment_blend_modes(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO), XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND))
		{
			passthrough.emplace<xr::passthrough_alpha_blend>();
		}
		else if (utils::contains(application::get_xr_extensions(), XR_FB_PASSTHROUGH_EXTENSION_NAME))
		{
			passthrough.emplace<xr::passthrough_fb>(instance, session);
		}
		else if (utils::contains(application::get_xr_extensions(), XR_HTC_PASSTHROUGH_EXTENSION_NAME))
		{
			passthrough.emplace<xr::passthrough_htc>(instance, session);
		}
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

	input.reset();
	left_hand.reset();
	right_hand.reset();

	renderer.reset();
	swapchains_lobby.clear();
	swapchains_controllers.clear();
	swapchains_depth.clear();
	swapchain_imgui = xr::swapchain();
	passthrough.emplace<std::monostate>();
	multicast.reset();
}

void scenes::lobby::on_session_state_changed(XrSessionState state)
{
	if (state == XR_SESSION_STATE_STOPPING)
		discover.reset();
	recenter_gui = true;
}

void scenes::lobby::on_reference_space_changed(XrReferenceSpaceType, XrTime)
{
	recenter_gui = true;
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
	                        "/interaction_profiles/oculus/touch_controller",
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

	                                {"recenter_left", "/user/hand/left/input/x/click"},
	                                {"recenter_right", "/user/hand/right/input/a/click"},
	                        },
	                },
	                suggested_binding{
	                        "/interaction_profiles/bytedance/pico_neo3_controller",
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

	                                {"recenter_left", "/user/hand/left/input/x/click"},
	                                {"recenter_right", "/user/hand/right/input/a/click"},
	                        },
	                },
	                suggested_binding{
	                        "/interaction_profiles/bytedance/pico4_controller",
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

	                                {"recenter_left", "/user/hand/left/input/x/click"},
	                                {"recenter_right", "/user/hand/right/input/a/click"},
	                        },
	                },
	                suggested_binding{
	                        "/interaction_profiles/htc/vive_focus3_controller",
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

	                                {"recenter_left", "/user/hand/left/input/x/click"},
	                                {"recenter_right", "/user/hand/right/input/a/click"},
	                        },
	                },
	                suggested_binding{
	                        "/interaction_profiles/khr/simple_controller",
	                        {
	                                {"left_aim", "/user/hand/left/input/aim/pose"},
	                                {"left_trigger", "/user/hand/left/input/select/click"},
	                                {"left_squeeze", "/user/hand/left/input/menu/click"},
	                                {"right_aim", "/user/hand/right/input/aim/pose"},
	                                {"right_trigger", "/user/hand/right/input/select/click"},
	                                {"right_squeeze", "/user/hand/right/input/menu/click"},
	                        },
	                },
	        }};

	return m;
}
