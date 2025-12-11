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
#include "constants.h"
#include "xr/body_tracker.h"
#include "xr/face_tracker.h"
#include "xr/fb_face_tracker2.h"
#include "xr/space.h"
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <magic_enum.hpp>
#include <openxr/openxr.h>
#define GLM_FORCE_RADIANS

#include "stream.h"

#include "application.h"
#include "audio/audio.h"
#include "boost/pfr/core.hpp"
#include "decoder/shard_accumulator.h"
#include "hardware.h"
#include "spdlog/spdlog.h"
#include "utils/contains.h"
#include "utils/named_thread.h"
#include "utils/ranges.h"
#include "wivrn_packets.h"
#include <algorithm>
#include <mutex>
#include <ranges>
#include <thread>
#include <vulkan/vulkan_raii.hpp>

#include "wivrn_config.h"
#if WIVRN_FEATURE_RENDERDOC
#include "vk/renderdoc.h"
#endif

using namespace wivrn;

// clang-format off
static const std::unordered_map<std::string, device_id> device_ids = {
	{"/user/hand/left/input/x/click",             device_id::X_CLICK},
	{"/user/hand/left/input/x/touch",             device_id::X_TOUCH},
	{"/user/hand/left/input/y/click",             device_id::Y_CLICK},
	{"/user/hand/left/input/y/touch",             device_id::Y_TOUCH},
	{"/user/hand/left/input/menu/click",          device_id::MENU_CLICK},
	{"/user/hand/left/input/squeeze/click",       device_id::LEFT_SQUEEZE_CLICK},
	{"/user/hand/left/input/squeeze/force",       device_id::LEFT_SQUEEZE_FORCE},
	{"/user/hand/left/input/squeeze/value",       device_id::LEFT_SQUEEZE_VALUE},
	{"/user/hand/left/input/trigger/value",       device_id::LEFT_TRIGGER_VALUE},
	{"/user/hand/left/input/trigger/click",       device_id::LEFT_TRIGGER_CLICK},
	{"/user/hand/left/input/trigger/touch",       device_id::LEFT_TRIGGER_TOUCH},
	{"/user/hand/left/input/trigger/proximity",   device_id::LEFT_TRIGGER_PROXIMITY},
	{"/user/hand/left/input/trigger/proximity_fb",device_id::LEFT_TRIGGER_PROXIMITY},
	{"/user/hand/left/input/trigger/proximity_meta",device_id::LEFT_TRIGGER_PROXIMITY},
	{"/user/hand/left/input/trigger/curl_fb",     device_id::LEFT_TRIGGER_CURL},
	{"/user/hand/left/input/trigger/curl_meta",   device_id::LEFT_TRIGGER_CURL},
	{"/user/hand/left/input/trigger_curl/value",  device_id::LEFT_TRIGGER_CURL},
	{"/user/hand/left/input/trigger/slide_fb",    device_id::LEFT_TRIGGER_SLIDE},
	{"/user/hand/left/input/trigger/slide_meta",  device_id::LEFT_TRIGGER_SLIDE},
	{"/user/hand/left/input/trigger_slide/value", device_id::LEFT_TRIGGER_SLIDE},
	{"/user/hand/left/input/trigger/force",       device_id::LEFT_TRIGGER_FORCE},
	{"/user/hand/left/input/thumbstick",          device_id::LEFT_THUMBSTICK_X},
	{"/user/hand/left/input/thumbstick/click",    device_id::LEFT_THUMBSTICK_CLICK},
	{"/user/hand/left/input/thumbstick/touch",    device_id::LEFT_THUMBSTICK_TOUCH},
	{"/user/hand/left/input/thumbrest/touch",     device_id::LEFT_THUMBREST_TOUCH},
	{"/user/hand/left/input/thumbrest/force",     device_id::LEFT_THUMBREST_FORCE},
	{"/user/hand/left/input/thumb_resting_surfaces/proximity",device_id::LEFT_THUMB_PROXIMITY},
	{"/user/hand/left/input/thumb_meta/proximity_meta",device_id::LEFT_THUMB_PROXIMITY},
	{"/user/hand/left/input/trackpad",            device_id::LEFT_TRACKPAD_X},
	{"/user/hand/left/input/trackpad/click",      device_id::LEFT_TRACKPAD_CLICK},
	{"/user/hand/left/input/trackpad/touch",      device_id::LEFT_TRACKPAD_TOUCH},
	{"/user/hand/left/input/trackpad/force",      device_id::LEFT_TRACKPAD_FORCE},
	{"/user/hand/left/input/stylus/force",        device_id::LEFT_STYLUS_FORCE},
	{"/user/hand/left/input/stylus_fb/force",     device_id::LEFT_STYLUS_FORCE},

	{"/user/hand/right/input/a/click",             device_id::A_CLICK},
	{"/user/hand/right/input/a/touch",             device_id::A_TOUCH},
	{"/user/hand/right/input/b/click",             device_id::B_CLICK},
	{"/user/hand/right/input/b/touch",             device_id::B_TOUCH},
	{"/user/hand/right/input/system/click",        device_id::SYSTEM_CLICK},
	{"/user/hand/right/input/squeeze/click",       device_id::RIGHT_SQUEEZE_CLICK},
	{"/user/hand/right/input/squeeze/force",       device_id::RIGHT_SQUEEZE_FORCE},
	{"/user/hand/right/input/squeeze/value",       device_id::RIGHT_SQUEEZE_VALUE},
	{"/user/hand/right/input/trigger/value",       device_id::RIGHT_TRIGGER_VALUE},
	{"/user/hand/right/input/trigger/click",       device_id::RIGHT_TRIGGER_CLICK},
	{"/user/hand/right/input/trigger/touch",       device_id::RIGHT_TRIGGER_TOUCH},
	{"/user/hand/right/input/trigger/proximity",   device_id::RIGHT_TRIGGER_PROXIMITY},
	{"/user/hand/right/input/trigger/proximity_fb",device_id::RIGHT_TRIGGER_PROXIMITY},
	{"/user/hand/right/input/trigger/proximity_meta",device_id::RIGHT_TRIGGER_PROXIMITY},
	{"/user/hand/right/input/trigger/curl_fb",     device_id::RIGHT_TRIGGER_CURL},
	{"/user/hand/right/input/trigger/curl_meta",   device_id::RIGHT_TRIGGER_CURL},
	{"/user/hand/right/input/trigger_curl/value",  device_id::RIGHT_TRIGGER_CURL},
	{"/user/hand/right/input/trigger/slide_fb",    device_id::RIGHT_TRIGGER_SLIDE},
	{"/user/hand/right/input/trigger/slide_meta",  device_id::RIGHT_TRIGGER_SLIDE},
	{"/user/hand/right/input/trigger_slide/value", device_id::RIGHT_TRIGGER_SLIDE},
	{"/user/hand/right/input/trigger/force",       device_id::RIGHT_TRIGGER_FORCE},
	{"/user/hand/right/input/thumbstick",          device_id::RIGHT_THUMBSTICK_X},
	{"/user/hand/right/input/thumbstick/click",    device_id::RIGHT_THUMBSTICK_CLICK},
	{"/user/hand/right/input/thumbstick/touch",    device_id::RIGHT_THUMBSTICK_TOUCH},
	{"/user/hand/right/input/thumbrest/touch",     device_id::RIGHT_THUMBREST_TOUCH},
	{"/user/hand/right/input/thumbrest/force",     device_id::RIGHT_THUMBREST_FORCE},
	{"/user/hand/right/input/thumb_resting_surfaces/proximity",device_id::RIGHT_THUMB_PROXIMITY},
	{"/user/hand/right/input/thumb_meta/proximity_meta",device_id::RIGHT_THUMB_PROXIMITY},
	{"/user/hand/right/input/trackpad",            device_id::RIGHT_TRACKPAD_X},
	{"/user/hand/right/input/trackpad/click",      device_id::RIGHT_TRACKPAD_CLICK},
	{"/user/hand/right/input/trackpad/touch",      device_id::RIGHT_TRACKPAD_TOUCH},
	{"/user/hand/right/input/trackpad/force",      device_id::RIGHT_TRACKPAD_FORCE},
	{"/user/hand/right/input/stylus/force",        device_id::RIGHT_STYLUS_FORCE},
	{"/user/hand/right/input/stylus_fb/force",     device_id::RIGHT_STYLUS_FORCE},

	// XR_EXT_hand_interaction
	{"/user/hand/left/input/pinch_ext/value",      device_id::LEFT_PINCH_VALUE},
	{"/user/hand/left/input/pinch_ext/ready_ext",  device_id::LEFT_PINCH_READY},
	{"/user/hand/left/input/aim_activate_ext/value",device_id::LEFT_AIM_ACTIVATE_VALUE},
	{"/user/hand/left/input/aim_activate_ext/ready_ext",device_id::LEFT_AIM_ACTIVATE_READY},
	{"/user/hand/left/input/grasp_ext/value",      device_id::LEFT_GRASP_VALUE},
	{"/user/hand/left/input/grasp_ext/ready_ext",  device_id::LEFT_GRASP_READY},

	{"/user/hand/right/input/pinch_ext/value",      device_id::RIGHT_PINCH_VALUE},
	{"/user/hand/right/input/pinch_ext/ready_ext",  device_id::RIGHT_PINCH_READY},
	{"/user/hand/right/input/aim_activate_ext/value",device_id::RIGHT_AIM_ACTIVATE_VALUE},
	{"/user/hand/right/input/aim_activate_ext/ready_ext",device_id::RIGHT_AIM_ACTIVATE_READY},
	{"/user/hand/right/input/grasp_ext/value",      device_id::RIGHT_GRASP_VALUE},
	{"/user/hand/right/input/grasp_ext/ready_ext",  device_id::RIGHT_GRASP_READY},
};
// clang-format on

static const std::array supported_color_formats = {
        vk::Format::eR8G8B8A8Srgb,
        vk::Format::eB8G8R8A8Srgb,
};

static const std::array supported_depth_formats{
        vk::Format::eD32Sfloat,
        vk::Format::eX8D24UnormPack32,
};

scenes::stream::stream(std::string server_name, scene & parent_scene) :
        scene_impl<stream>(supported_color_formats, supported_depth_formats, parent_scene),
        apps{*this, std::move(server_name)}
{
	auto views = system.view_configuration_views(viewconfig);
	width = views[0].recommendedImageRectWidth;
	height = views[0].recommendedImageRectHeight;
}

static from_headset::visibility_mask_changed::masks get_visibility_mask(xr::instance & inst, xr::session & session, int view)
{
	assert(inst.has_extension(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME));
	static auto xrGetVisibilityMaskKHR = inst.get_proc<PFN_xrGetVisibilityMaskKHR>("xrGetVisibilityMaskKHR");

	from_headset::visibility_mask_changed::masks res{};
	for (auto [type, mask]: utils::enumerate(res))
	{
		XrVisibilityMaskKHR xr_mask{
		        .type = XR_TYPE_VISIBILITY_MASK_KHR,
		};
		CHECK_XR(xrGetVisibilityMaskKHR(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view, XrVisibilityMaskTypeKHR(type + 1), &xr_mask));
		mask.vertices.resize(xr_mask.vertexCountOutput);
		mask.indices.resize(xr_mask.indexCountOutput);
		xr_mask = {
		        .type = XR_TYPE_VISIBILITY_MASK_KHR,
		        .vertexCapacityInput = uint32_t(mask.vertices.size()),
		        .vertices = mask.vertices.data(),
		        .indexCapacityInput = uint32_t(mask.indices.size()),
		        .indices = mask.indices.data(),
		};
		CHECK_XR(xrGetVisibilityMaskKHR(session, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, view, XrVisibilityMaskTypeKHR(type + 1), &xr_mask));
		mask.vertices.resize(xr_mask.vertexCountOutput);
		mask.indices.resize(xr_mask.indexCountOutput);
	}
	return res;
}

std::shared_ptr<scenes::stream> scenes::stream::create(std::unique_ptr<wivrn_session> network_session, float guessed_fps, std::string server_name, scene & parent_scene)
{
	std::shared_ptr<stream> self{new stream{std::move(server_name), parent_scene}};
	self->network_session = std::move(network_session);

	self->network_session->send_control([&]() {
		from_headset::headset_info_packet info{
		        .language = application::get_messages_info().language,
		        .country = application::get_messages_info().country,
		        .variant = application::get_messages_info().variant,
		};

		{
			auto [flags, views] = self->session.locate_views(
			        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			        self->instance.now(),
			        application::space(xr::spaces::view));

			assert(views.size() == info.fov.size());

			for (auto [i, j]: std::views::zip(views, info.fov))
				j = i.fov;
		}

		const auto & config = application::get_config();

		{
			auto view = self->system.view_configuration_views(self->viewconfig)[0];
			view = override_view(view, guess_model());

			info.render_eye_width = view.recommendedImageRectWidth * config.resolution_scale;
			info.render_eye_height = view.recommendedImageRectHeight * config.resolution_scale;
		}

		{
			auto scale = config.get_stream_scale();
			info.stream_eye_width = info.render_eye_width * scale;
			info.stream_eye_height = info.render_eye_height * scale;
		}

		if (self->instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
		{
			info.available_refresh_rates = self->session.get_refresh_rates();
			// I can't find anything in specification the ensures it won't be empty
			if (not info.available_refresh_rates.empty())
			{
				if (config.preferred_refresh_rate and (config.preferred_refresh_rate == 0 or utils::contains(info.available_refresh_rates, *config.preferred_refresh_rate)))
				{
					info.settings.preferred_refresh_rate = *config.preferred_refresh_rate;
					info.settings.minimum_refresh_rate = config.minimum_refresh_rate.value_or(0);
				}
				else
				{
					// Default to highest refresh rate
					info.settings.preferred_refresh_rate = info.available_refresh_rates.back();
				}
			}
		}

		if (info.available_refresh_rates.empty())
		{
			spdlog::warn("Unable to detect refresh rates");
			info.available_refresh_rates = {guessed_fps};
			info.settings.preferred_refresh_rate = guessed_fps;
		}

		info.settings.bitrate_bps = config.bitrate_bps;

		info.hand_tracking = config.check_feature(feature::hand_tracking);
		info.eye_gaze = config.check_feature(feature::eye_gaze);

		if (config.check_feature(feature::face_tracking))
		{
			switch (self->system.face_tracker_supported())
			{
				case xr::face_tracker_type::none:
					info.face_tracking = from_headset::face_type::none;
					break;
				case xr::face_tracker_type::android:
					info.face_tracking = from_headset::face_type::android;
					break;
				case xr::face_tracker_type::fb:
				case xr::face_tracker_type::pico:
					info.face_tracking = from_headset::face_type::fb2;
					break;
				case xr::face_tracker_type::htc:
					info.face_tracking = from_headset::face_type::htc;
					break;
			}
		}

		info.num_generic_trackers = 0;
		if (config.check_feature(feature::body_tracking))
		{
			switch (self->system.body_tracker_supported())
			{
				case xr::body_tracker_type::none:
					break;
				case xr::body_tracker_type::fb:
					info.num_generic_trackers = xr::fb_body_tracker::get_whitelisted_joints(config.fb_lower_body, config.fb_hip).size();
					break;
				case xr::body_tracker_type::htc:
					info.num_generic_trackers = application::get_generic_trackers().size();
					break;
				case xr::body_tracker_type::pico:
					info.num_generic_trackers = xr::pico_body_tracker::joint_whitelist.size();
					break;
			}
		}

		info.palm_pose = application::space(xr::spaces::palm_left) or application::space(xr::spaces::palm_right);
		info.passthrough = self->system.passthrough_supported() != xr::passthrough_type::none;
		info.system_name = std::string(self->system.properties().systemName);

		audio::get_audio_description(info);
		if (not(config.check_feature(feature::microphone)))
			info.microphone = {};

		if (config.codec)
		{
			info.supported_codecs = {*config.codec};
			switch (*config.codec)
			{
				case h264:
				case raw:
					break;
				case h265:
				case av1:
					info.bit_depth = config.bit_depth;
			}
		}
		else
			info.supported_codecs = decoder::supported_codecs();

		return info;
	}());

	self->network_session->send_control(from_headset::session_state_changed{
	        .state = application::get_session_state(),
	});

	if (self->instance.has_extension(XR_KHR_VISIBILITY_MASK_EXTENSION_NAME))
	{
		for (uint8_t view = 0; view < view_count; ++view)
		{
			try
			{
				self->network_session->send_control(from_headset::visibility_mask_changed{
				        .data = get_visibility_mask(self->instance, self->session, view),
				        .view_index = view});
			}
			catch (std::exception & e)
			{
				spdlog::warn("Failed to get visibility mask: ", e.what());
			}
		}
	}

	{
		const auto & config = application::get_config();
		self->override_foveation_enable = config.override_foveation_enable;
		self->override_foveation_pitch = config.override_foveation_pitch;
		self->override_foveation_distance = config.override_foveation_distance;

		if (self->override_foveation_enable)
			self->network_session->send_control(from_headset::override_foveation_center{
			        .enabled = self->override_foveation_enable,
			        .pitch = self->override_foveation_pitch,
			        .distance = self->override_foveation_distance,
			});
	}

	self->network_thread = utils::named_thread("network_thread", &stream::process_packets, self.get());

	self->command_buffer = std::move(self->device.allocateCommandBuffers({
	        .commandPool = *self->commandpool,
	        .level = vk::CommandBufferLevel::ePrimary,
	        .commandBufferCount = 1,
	})[0]);

	self->fence = self->device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});

	// Look up the XrActions for haptics
	for (auto [id, path, output]: {
	             std::tuple(device_id::LEFT_CONTROLLER_HAPTIC, "/user/hand/left", "/output/haptic"),
	             std::tuple(device_id::RIGHT_CONTROLLER_HAPTIC, "/user/hand/right", "/output/haptic"),

	             std::tuple(device_id::LEFT_TRIGGER_HAPTIC, "/user/hand/left", "/output/haptic_trigger"),
	             std::tuple(device_id::RIGHT_TRIGGER_HAPTIC, "/user/hand/right", "/output/haptic_trigger"),
	             std::tuple(device_id::LEFT_TRIGGER_HAPTIC, "/user/hand/left", "/output/haptic_trigger_fb"),
	             std::tuple(device_id::RIGHT_TRIGGER_HAPTIC, "/user/hand/right", "/output/haptic_trigger_fb"),

	             std::tuple(device_id::LEFT_THUMB_HAPTIC, "/user/hand/left", "/output/haptic_thumb"),
	             std::tuple(device_id::RIGHT_THUMB_HAPTIC, "/user/hand/right", "/output/haptic_thumb"),
	             std::tuple(device_id::LEFT_THUMB_HAPTIC, "/user/hand/left", "/output/haptic_thumb_fb"),
	             std::tuple(device_id::RIGHT_THUMB_HAPTIC, "/user/hand/right", "/output/haptic_thumb_fb")})
	{
		if (auto action = application::get_action(std::string(path) + output); action.first)
		{
			self->haptics_actions.emplace(id, haptics_action{
			                                          .action = action.first,
			                                          .path = self->instance.string_to_path(path),
			                                  });
		}
	}

	// Look up the XrActions for input
	for (const auto & [action, action_type, name]: application::inputs())
	{
		auto it = device_ids.find(name);

		if (it == device_ids.end())
			continue;

		self->input_actions.emplace_back(it->second, action, action_type);
	}

	spdlog::info("Using format {}", vk::to_string(self->swapchain_format));

	self->query_pool = vk::raii::QueryPool(
	        self->device,
	        vk::QueryPoolCreateInfo{
	                .queryType = vk::QueryType::eTimestamp,
	                .queryCount = size_gpu_timestamps,
	        });

	self->wifi = application::get_wifi_lock().get_wifi_lock();

	return self;
}

void scenes::stream::on_focused()
{
	gui_status_last_change = instance.now();

	std::string profile = controller_name();
	input.emplace(
	        *this,
	        "assets://controllers/" + profile + "/profile.json",
	        layer_controllers,
	        layer_rays);

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

	std::array imgui_inputs{
	        imgui_context::controller{
	                .aim = get_action_space("left_aim"),
	                .offset = input->offset[xr::spaces::aim_left],
	                .trigger = get_action("left_trigger").first,
	                .squeeze = get_action("left_squeeze").first,
	                .scroll = get_action("left_scroll").first,
	                .haptic_output = get_action("left_haptic").first,
	        },
	        imgui_context::controller{
	                .aim = get_action_space("right_aim"),
	                .offset = input->offset[xr::spaces::aim_right],
	                .trigger = get_action("right_trigger").first,
	                .squeeze = get_action("right_squeeze").first,
	                .scroll = get_action("right_scroll").first,
	                .haptic_output = get_action("right_haptic").first,
	        },
	};

	xr::swapchain swapchain_imgui(
	        instance,
	        session,
	        device,
	        swapchain_format,
	        1800,
	        1000);

	imgui_context::viewport vp{
	        .space = xr::spaces::world,
	        // Position and orientation are set at each frame
	        .size = {1.2, 0.6666},
	        .vp_origin = {0, 0},
	        .vp_size = {1800, 1000},
	};

	imgui_ctx.emplace(physical_device,
	                  device,
	                  queue_family_index,
	                  queue,
	                  imgui_inputs,
	                  std::move(swapchain_imgui),
	                  std::vector{vp},
	                  image_cache);

	if (application::get_config().enable_stream_gui)
	{
		plots_toggle_1 = get_action("plots_toggle_1").first;
		plots_toggle_2 = get_action("plots_toggle_2").first;
	}
	recenter_left = get_action("recenter_left").first;
	recenter_right = get_action("recenter_right").first;
	settings_adjust = get_action("settings_adjust").first;
	foveation_distance = get_action("foveation_distance").first;
	foveation_ok = get_action("foveation_ok").first;
	foveation_cancel = get_action("foveation_cancel").first;

	assert(video_stream_description);
	std::unique_lock lock(decoder_mutex);

	if (application::get_config().high_power_mode)
	{
		session.set_performance_level(XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
		session.set_performance_level(XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
	}
	else
	{
		session.set_performance_level(XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT);
		session.set_performance_level(XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT);
	}
}

void scenes::stream::on_unfocused()
{
	renderer->wait_idle(); // Must be before the scene data because the renderer uses its descriptor sets;
	world.clear();
	input.reset();
	clear_swapchains();
	left_hand.reset();
	right_hand.reset();

	imgui_ctx.reset();
}

scenes::stream::~stream()
{
	exit();

	if (tracking_thread && tracking_thread->joinable())
		tracking_thread->join();

	if (network_thread.joinable())
		network_thread.join();
}

void scenes::stream::push_blit_handle(shard_accumulator * decoder, std::shared_ptr<shard_accumulator::blit_handle> handle)
{
	assert(handle);
	if (!application::is_visible())
		return;

	{
		std::shared_lock lock(decoder_mutex);
		std::unique_lock frame_lock(frames_mutex);
		auto stream = handle->feedback.stream_index;
		if (stream < decoders.size())
		{
			if (decoder != decoders[stream].decoder.get())
				return;
			handle->feedback.received_from_decoder = instance.now();
			std::swap(handle, decoders[stream].latest_frames[handle->feedback.frame_index % decoders[stream].latest_frames.size()]);
		}

		if (state_ != state::streaming and not(decoders[0].empty() and decoders[1].empty()))
		{
			state_ = state::streaming;
			spdlog::info("Stream scene ready at t={}", instance.now());
		}
	}

	if (handle and not handle->feedback.blitted)
	{
		send_feedback(handle->feedback);
	}
}

bool scenes::stream::accumulator_images::empty() const
{
	for (const auto & frame: latest_frames)
	{
		if (frame)
			return false;
	}
	return true;
}

std::array<std::shared_ptr<shard_accumulator::blit_handle>, 3> scenes::stream::common_frame(XrTime display_time)
{
	if (decoders.empty())
		return {};
	std::unique_lock lock(frames_mutex);
	thread_local std::vector<shard_accumulator::blit_handle *> common_frames;
	common_frames.clear();
	const bool alpha = decoders[0].latest_frames[0] and decoders[0].latest_frames[0]->view_info.alpha;
	for (size_t i = 0; i < view_count + alpha; ++i)
	{
		if (i == 0)
		{
			for (const auto & h: decoders[i].latest_frames)
				if (h)
					common_frames.push_back(h.get());
		}
		else
		{
			// clang-format off
			std::erase_if(common_frames,
				[this, i](auto & left)
				{
					return std::ranges::none_of(
						decoders[i].latest_frames,
						[&left](auto & right)
						{
							return right and left->feedback.frame_index == right->feedback.frame_index;
						});
				});
			// clang-format on
		}
	}
	std::array<std::shared_ptr<shard_accumulator::blit_handle>, view_count + 1> result;
	if (not common_frames.empty())
	{
		auto min = std::ranges::min_element(common_frames,
		                                    std::ranges::less{},
		                                    [display_time](auto frame) {
			                                    if (not frame)
				                                    return std::numeric_limits<XrTime>::max();
			                                    return std::abs(frame->view_info.display_time - display_time);
		                                    });

		assert(*min);
		auto frame_index = (*min)->feedback.frame_index;
		for (auto [i, decoder]: utils::enumerate(decoders))
		{
			if (alpha or i < view_count)
				result[i] = decoder.frame(frame_index);
		}
	}
	else
	{
		spdlog::warn("Failed to find a common frame for all decoders, dumping available frames per decoder");
		for (const auto & decoder: decoders)
		{
			std::string frames;
			for (const auto & frame: decoder.latest_frames)
			{
				if (frame)
					frames += " " + std::to_string(frame->feedback.frame_index);
				else
					frames += " -";
			}
			spdlog::warn(frames);
		}

		for (auto [i, decoder]: utils::enumerate(decoders))
		{
			if (alpha or i < view_count)
			{
				auto min = std::ranges::min_element(decoder.latest_frames,
				                                    std::ranges::less{},
				                                    [display_time](auto frame) {
					                                    if (not frame)
						                                    return std::numeric_limits<XrTime>::max();
					                                    return std::abs(frame->view_info.display_time - display_time);
				                                    });
				result[i] = *min;
			}
		}
	}
	return result;
}

std::shared_ptr<shard_accumulator::blit_handle> scenes::stream::accumulator_images::frame(uint64_t id) const
{
	for (auto it = latest_frames.rbegin(); it != latest_frames.rend(); ++it)
	{
		if (not *it)
			continue;

		if ((*it)->feedback.frame_index != id)
			continue;

		return *it;
	}
	return nullptr;
}

void scenes::stream::update_gui_position(xr::spaces controller)
{
	auto aim = application::locate_controller(
	        application::space(controller),
	        application::space(xr::spaces::view),
	        predicted_display_time);

	if (not aim)
		return;

	auto [offset_position, offset_orientation] = input->offset[controller];

	auto head_controller_position = aim->first + glm::mat3_cast(aim->second * offset_orientation) * offset_position;
	auto head_controller_orientation = aim->second * offset_orientation;
	auto head_controller_direction = -glm::column(glm::mat3_cast(head_controller_orientation), 2);

	if (not recentering_context)
	{
		// First frame of recentering: get the GUI position relative to the controller

		// Compute the intersection of the ray with the GUI
		auto gui_controller_direction = glm::conjugate(head_gui_orientation) * head_controller_direction;
		auto gui_controller_position = glm::conjugate(head_gui_orientation) * (head_controller_position - head_gui_position);

		float lambda = -gui_controller_position.z / gui_controller_direction.z;
		auto gui_intersection = gui_controller_position + lambda * gui_controller_direction;

		auto viewport_size = imgui_ctx->layers()[0].size;
		if (std::isnan(lambda) or lambda < 0 or
		    std::abs(gui_intersection.x) > viewport_size.x / 2 or
		    std::abs(gui_intersection.y) > viewport_size.y / 2)
		{
			// Reset the relative GUI position if the ray does not intersect
			recentering_context.emplace(controller, glm::vec3{0, 0, -1}, glm::quat{1, 0, 0, 0});
		}
		else
		{
			glm::vec3 controller_gui_position = glm::conjugate(head_controller_orientation) * (head_gui_position - head_controller_position);
			glm::quat controller_gui_orientation = glm::conjugate(head_controller_orientation) * head_gui_orientation;

			recentering_context.emplace(controller, controller_gui_position, controller_gui_orientation);
		}
	}
	else
	{
		// Subsequent frames of recentering: keep the GUI locked to the controller
		auto [_, controller_gui_position, controller_gui_orientation] = *recentering_context;

		head_gui_position = head_controller_position + head_controller_orientation * controller_gui_position;
		head_gui_orientation = head_controller_orientation * controller_gui_orientation;
	}
}

bool scenes::stream::is_gui_interactable() const
{
	switch (gui_status)
	{
		case gui_status::stats:
		case gui_status::settings:
		case gui_status::bitrate_settings:
		case gui_status::foveation_settings:
		case gui_status::applications:
		case gui_status::application_launcher:
			return true;

		case gui_status::hidden:
		case gui_status::overlay_only:
		case gui_status::compact:
			return false;
	}

	assert(false);
	__builtin_unreachable();
}

void scenes::stream::render(const XrFrameState & frame_state)
{
	if (exiting)
		application::pop_scene();

	display_time_phase = frame_state.predictedDisplayTime % frame_state.predictedDisplayPeriod;
	display_time_period = frame_state.predictedDisplayPeriod;
	real_display_period = last_display_time ? frame_state.predictedDisplayTime - last_display_time : frame_state.predictedDisplayPeriod;
	last_display_time = frame_state.predictedDisplayTime;

	std::shared_lock lock(decoder_mutex);
	if (not frame_state.shouldRender or (decoders[0].empty() and decoders[1].empty()))
	{
		// TODO: stop/restart video stream
		session.begin_frame();
		session.end_frame(frame_state.predictedDisplayTime, {});
		return;
	}

	if (state_ == state::stalled)
	{
		network_session->send_control(from_headset::get_application_list{
		        .language = application::get_messages_info().language,
		        .country = application::get_messages_info().country,
		        .variant = application::get_messages_info().variant,
		});

		gui_status = stream::gui_status::hidden;
		application::pop_scene();
	}

	if (device.waitForFences(*fence, VK_TRUE, UINT64_MAX) == vk::Result::eTimeout)
		throw std::runtime_error("Vulkan fence timeout");

	device.resetFences(*fence);

	// We don't need those after vkWaitForFences
	current_blit_handles.fill(nullptr);

	gpu_timestamps timestamps;
	if (query_pool_filled)
	{
		auto [res, timestamps2] = query_pool.getResults<uint64_t>(
		        0,
		        size_gpu_timestamps,
		        size_gpu_timestamps * sizeof(uint64_t),
		        sizeof(uint64_t),
		        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);

		if (res == vk::Result::eSuccess)
		{
			boost::pfr::for_each_field(timestamps, [n = 1, &timestamps2](float & t) mutable {
				t = (timestamps2[n++] - timestamps2[0]) * application::get_physical_device_properties().limits.timestampPeriod / 1e9;
			});
		}
	}

	session.begin_frame();

	std::array<int, view_count> image_indices;

#if WIVRN_FEATURE_RENDERDOC
	renderdoc_begin(*vk_instance);
#endif
	command_buffer.reset();
	command_buffer.begin(vk::CommandBufferBeginInfo{
	        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
	});

	// Keep a reference to the resources needed to blit the images until vkWaitForFences

	command_buffer.resetQueryPool(*query_pool, 0, size_gpu_timestamps);
	command_buffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, *query_pool, 0);

	// Search for frame with desired display time on all decoders
	// If no such frame exists, use the latest frame for each decoder
	current_blit_handles = common_frame(frame_state.predictedDisplayTime);
	std::array<XrPosef, view_count> pose;
	std::array<XrFovf, view_count> fov;
	std::array<wivrn::to_headset::foveation_parameter, view_count> foveation;
	bool use_alpha = false;

	std::array<stream_defoveator::input, view_count> images;
	for (size_t i = 0; i < view_count + use_alpha; ++i)
	{
		auto & blit_handle = current_blit_handles[i];
		if (not blit_handle)
			continue;

		blit_handle->feedback.blitted = instance.now();
		if (blit_handle->feedback.blitted - blit_handle->feedback.received_from_decoder > 1'000'000'000)
			state_ = stream::state::stalled;
		++blit_handle->feedback.times_displayed;
		blit_handle->feedback.displayed = frame_state.predictedDisplayTime;

		use_alpha = blit_handle->view_info.alpha;

		if (blit_handle->current_layout == vk::ImageLayout::eUndefined)
		{
			vk::ImageMemoryBarrier barrier{
			        .srcAccessMask = vk::AccessFlagBits::eNone,
			        .dstAccessMask = vk::AccessFlagBits::eMemoryRead,
			        .oldLayout = vk::ImageLayout::eUndefined,
			        .newLayout = vk::ImageLayout::eGeneral,
			        .image = blit_handle->image,
			        .subresourceRange = {
			                .aspectMask = vk::ImageAspectFlagBits::eColor,
			                .levelCount = 1,
			                .layerCount = 1,
			        },
			};

			command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eAllCommands, {}, {}, {}, barrier);
			blit_handle->current_layout = vk::ImageLayout::eGeneral;
		}

		if (i < view_count)
		{
			foveation[i] = blit_handle->view_info.foveation[i];
			pose[i] = blit_handle->view_info.pose[i];
			fov[i] = blit_handle->view_info.fov[i];
			// colour image
			images[i] = {
			        .rgb = blit_handle->image_view,
			        .sampler_rgb = decoders[i].decoder->sampler(),
			        .rect_rgb = {
			                .extent = blit_handle->extent,
			        },
			        .layout_rgb = blit_handle->current_layout,
			};
		}
		else
		{
			// alpha image, must set for each view
			for (int j = 0; j < view_count; ++j)
			{
				images[j].a = blit_handle->image_view;
				images[j].sampler_a = decoders[i].decoder->sampler();
				images[j].rect_a = vk::Rect2D{
				        // in full size pixels
				        .offset = {
				                .x = j * video_stream_description->width,
				        },
				        .extent = {
				                .width = blit_handle->extent.width * 2,
				                .height = blit_handle->extent.height * 2,
				        },
				};
				images[j].layout_a = blit_handle->current_layout;
			}
		}
	}

	XrExtent2Di extents[view_count];
	{
		int32_t max_width = 0;
		int32_t max_height = 0;
		for (size_t i = 0; i < view_count; ++i)
		{
			extents[i] = defoveator->defoveated_size(foveation[i]);
			max_width = std::max(max_width, extents[i].width);
			max_height = std::max(max_height, extents[i].height);
		}
		if (not swapchain)
			setup_reprojection_swapchain(max_width, max_height);
		else if (swapchain.width() < max_width or swapchain.height() < max_height)
		{
			// If the defoveated image is larger than the swapchain, try to reallocate one
			try
			{
				spdlog::info("Recreating swapchain, from {}x{} to {}x{}",
				             swapchain.width(),
				             swapchain.height(),
				             max_width,
				             max_height);
				setup_reprojection_swapchain(max_width, max_height);
			}
			catch (std::exception & e)
			{
				spdlog::warn("failed to increase swapchain size");
				for (size_t i = 0; i < view_count; ++i)
				{
					extents[i].width = std::min(extents[i].width, swapchain.width());
					extents[i].height = std::min(extents[i].height, swapchain.height());
				}
			}
		}
	}
	assert(swapchain);
	// defoveate the image
	int image_index = swapchain.acquire();
	swapchain.wait();
	defoveator->defoveate(command_buffer, foveation, images, image_index);

	command_buffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, *query_pool, 1);

	command_buffer.end();
	vk::SubmitInfo submit_info;
	submit_info.setCommandBuffers(*command_buffer);

	std::vector<vk::Semaphore> semaphores;
	std::vector<uint64_t> semaphore_vals;
	std::vector<vk::PipelineStageFlags> wait_stages;
	for (auto b: current_blit_handles)
	{
		if (b and b->semaphore)
		{
			assert(b->semaphore_val);
			semaphores.push_back(b->semaphore);
			semaphore_vals.push_back(*b->semaphore_val);
			wait_stages.push_back(vk::PipelineStageFlagBits::eFragmentShader);
		}
	}
	submit_info.setWaitDstStageMask(wait_stages);
	submit_info.setWaitSemaphores(semaphores);
	vk::TimelineSemaphoreSubmitInfo sem_info{
	        .waitSemaphoreValueCount = uint32_t(semaphore_vals.size()),
	        .pWaitSemaphoreValues = semaphore_vals.data(),
	};
	submit_info.pNext = &sem_info;

	queue.lock()->submit(submit_info, *fence);
#if WIVRN_FEATURE_RENDERDOC
	renderdoc_end(*vk_instance);
#endif
	swapchain.release();

	std::vector<XrCompositionLayerProjectionView> layer_view(view_count);

	if (use_alpha)
		session.enable_passthrough(system);
	else
		session.disable_passthrough();

	render_start(use_alpha, frame_state.predictedDisplayTime);

	// Add the layer with the streamed content
	for (uint32_t view = 0; view < view_count; view++)
	{
		layer_view[view] =
		        {
		                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
		                .pose = pose[view],
		                .fov = fov[view],

		                .subImage = {
		                        .swapchain = swapchain,
		                        .imageRect = {
		                                .offset = {0, 0},
		                                .extent = extents[view],
		                        },
		                        .imageArrayIndex = view,
		                },
		        };
	}
	add_projection_layer(
	        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
	        application::space(xr::spaces::world),
	        std::move(layer_view));

	if (composition_layer_color_scale_bias_supported)
	{
		switch (gui_status)
		{
			case gui_status::hidden:
			case gui_status::bitrate_settings:
			case gui_status::foveation_settings:
			case gui_status::compact:
			case gui_status::overlay_only:
				dimming = dimming - frame_state.predictedDisplayPeriod / (1e9 * constants::stream::fade_duration);
				break;
			case gui_status::stats:
			case gui_status::settings:
			case gui_status::applications:
			case gui_status::application_launcher:
				dimming = dimming + frame_state.predictedDisplayPeriod / (1e9 * constants::stream::fade_duration);
				break;
		}

		dimming = std::clamp<float>(dimming, 0, 1);
		float x = dimming * dimming * (3 - 2 * dimming); // Easing function

		float scale = std::lerp(1, constants::stream::dimming_scale, x);
		float bias = std::lerp(0, constants::stream::dimming_bias, x);

		set_color_scale_bias({scale, scale, scale, 1}, {bias, bias, bias, 0});
	}

	if (const configuration::openxr_post_processing_settings openxr_post_processing = application::get_config().openxr_post_processing;
	    (openxr_post_processing.sharpening | openxr_post_processing.super_sampling) > 0)
		set_layer_settings(openxr_post_processing.sharpening | openxr_post_processing.super_sampling);

	accumulate_metrics(frame_state.predictedDisplayTime, current_blit_handles, timestamps);

	draw_gui(frame_state.predictedDisplayTime, frame_state.predictedDisplayPeriod);

	try
	{
		render_end();
	}
	catch (std::system_error & e)
	{
		if (e.code().category() == xr::error_category() and e.code().value() == XR_ERROR_POSE_INVALID)
			spdlog::info("Invalid pose submitted");
		else
			throw;
	}

	// Network operations may be blocking, do them once everything was submitted
	{
		// Keep a copy of the feedback packets as they can be modified if they're encrypted
		std::vector<from_headset::feedback> feedbacks;
		std::vector<serialization_packet> packets;

		feedbacks.reserve(current_blit_handles.size());
		packets.reserve(current_blit_handles.size());

		for (const auto & handle: current_blit_handles)
		{
			if (handle)
			{
				auto & packet = packets.emplace_back();
				wivrn_session::control_socket_t::serialize(packet, feedbacks.emplace_back(handle->feedback));
			}
		}
		if (not packets.empty())
		{
			try
			{
				network_session->send_control(std::span(packets));
			}
			catch (std::exception & e)
			{
				spdlog::warn("Exception while sending feedback packet: {}", e.what());
			}
		}
	}

	read_actions();

	if (plots_toggle_1 and plots_toggle_2)
	{
		XrActionStateGetInfo get_info{
		        .type = XR_TYPE_ACTION_STATE_GET_INFO,
		        .action = plots_toggle_1,
		};

		XrActionStateBoolean state_1{XR_TYPE_ACTION_STATE_BOOLEAN};
		CHECK_XR(xrGetActionStateBoolean(session, &get_info, &state_1));
		get_info.action = plots_toggle_2;
		XrActionStateBoolean state_2{XR_TYPE_ACTION_STATE_BOOLEAN};
		CHECK_XR(xrGetActionStateBoolean(session, &get_info, &state_2));

		if (state_1.currentState and state_2.currentState and (state_1.changedSinceLastSync or state_2.changedSinceLastSync))
		{
			switch (gui_status)
			{
				case gui_status::hidden:
				case gui_status::compact:
				case gui_status::overlay_only:
					gui_status = next_gui_status;
					break;

				case gui_status::stats:
				case gui_status::settings:
				case gui_status::bitrate_settings:
				case gui_status::foveation_settings:
				case gui_status::applications:
				case gui_status::application_launcher:
					gui_status = gui_status::hidden;
					break;
			}
		}
	}

	query_pool_filled = true;
}

void scenes::stream::exit()
{
	exiting = true;
}

void scenes::stream::setup(const to_headset::video_stream_description & description)
{
	session.set_refresh_rate(description.fps);

	spdlog::info("Creating decoders, size {}x{}", description.width, description.height);
	std::unique_lock lock(decoder_mutex);
	video_stream_description = description;

	for (const auto & [stream_index, item]: utils::enumerate(decoders))
	{
		item = accumulator_images{
		        .decoder = std::make_unique<shard_accumulator>(device, physical_device, instance, queue_family_index, description, shared_from_this(), stream_index),
		};
	}
}

void scenes::stream::setup_reprojection_swapchain(uint32_t swapchain_width, uint32_t swapchain_height)
{
	assert(swapchain_width);
	assert(swapchain_height);
	device.waitIdle();
	session.set_refresh_rate(video_stream_description->fps);

	auto views = system.view_configuration_views(viewconfig);

	swapchain = xr::swapchain(instance, session, device, swapchain_format, swapchain_width, swapchain_height, 1, views.size());
	spdlog::info("Created stream swapchain: {}x{}", swapchain.width(), swapchain.height());
	for (auto view: views)
	{
		if (swapchain.width() > view.maxImageRectWidth or swapchain.height() > view.maxImageRectHeight)
			spdlog::warn("Swapchain size larger than maximum {}x{}", view.maxImageRectWidth, view.maxImageRectHeight);
	}

	spdlog::info("Initializing reprojector");
	vk::Extent2D extent = {(uint32_t)swapchain.width(), (uint32_t)swapchain.height()};
	std::vector<vk::Image> swapchain_images;
	for (auto & image: swapchain.images())
		swapchain_images.push_back(image.image);

	defoveator.emplace(
	        device,
	        physical_device,
	        swapchain_images,
	        extent,
	        swapchain.format());
}

scene::meta & scenes::stream::get_meta_scene()
{
	static meta m{
	        .name = "Stream",
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

	                {"plots_toggle_1", XR_ACTION_TYPE_BOOLEAN_INPUT},
	                {"plots_toggle_2", XR_ACTION_TYPE_BOOLEAN_INPUT},

	                {"recenter_left", XR_ACTION_TYPE_BOOLEAN_INPUT},
	                {"recenter_right", XR_ACTION_TYPE_BOOLEAN_INPUT},

	                {"settings_adjust", XR_ACTION_TYPE_FLOAT_INPUT},
	                {"foveation_distance", XR_ACTION_TYPE_FLOAT_INPUT},
	                {"foveation_ok", XR_ACTION_TYPE_BOOLEAN_INPUT},
	                {"foveation_cancel", XR_ACTION_TYPE_BOOLEAN_INPUT},
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
	                                {"settings_adjust", "/user/hand/right/input/thumbstick/y"},
	                                {"foveation_distance", "/user/hand/left/input/thumbstick/y"},
	                                {"foveation_ok", "/user/hand/right/input/a/click"},
	                                {"foveation_cancel", "/user/hand/right/input/b/click"},

	                                {"plots_toggle_1", "/user/hand/left/input/thumbstick/click"},
	                                {"plots_toggle_2", "/user/hand/right/input/thumbstick/click"},
	                        },
	                },
	                suggested_binding{
	                        {
	                                "/interaction_profiles/khr/simple_controller",
	                        },
	                        {},
	                },
	        },
	};

	return m;
}

void scenes::stream::on_xr_event(const xr::event & event)
{
	switch (event.header.type)
	{
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			if (event.space_changed_pending.referenceSpaceType == XrReferenceSpaceType::XR_REFERENCE_SPACE_TYPE_LOCAL)
				recenter_requested = true;
			break;
		case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB:
			network_session->send_control(from_headset::refresh_rate_changed{
			        .from = event.refresh_rate_changed.fromDisplayRefreshRate,
			        .to = event.refresh_rate_changed.toDisplayRefreshRate,
			});
			break;
		case XR_TYPE_EVENT_DATA_VISIBILITY_MASK_CHANGED_KHR:
			network_session->send_control(from_headset::visibility_mask_changed{
			        .data = get_visibility_mask(instance, session, event.visibility_mask_changed.viewIndex),
			        .view_index = uint8_t(event.visibility_mask_changed.viewIndex),
			});
			break;
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			// Override session state if the GUI is interactable
			if (event.state_changed.state == XR_SESSION_STATE_FOCUSED and is_gui_interactable())
				network_session->send_control(from_headset::session_state_changed{
				        .state = XR_SESSION_STATE_VISIBLE,
				});
			else
				network_session->send_control(from_headset::session_state_changed{
				        .state = event.state_changed.state,
				});
			break;
		case XR_TYPE_EVENT_DATA_USER_PRESENCE_CHANGED_EXT:
			network_session->send_control(from_headset::user_presence_changed{
			        .present = (bool)event.user_presence_changed.isUserPresent,
			});
			break;
		case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
			on_interaction_profile_changed(event.interaction_profile_changed);
			break;
		default:
			break;
	}
}

bool scenes::stream::forward_hid_input(from_headset::hid::input_t packet)
{
	if (not hid_forwarding)
		return false;
	network_session->send_control(from_headset::hid::input{packet});
	return true;
}

bool scenes::stream::on_input_key_down(uint8_t key_code)
{
	return forward_hid_input(from_headset::hid::key_down{key_code});
}
bool scenes::stream::on_input_key_up(uint8_t key_code)
{
	return forward_hid_input(from_headset::hid::key_up{key_code});
}
bool scenes::stream::on_input_mouse_move(float x, float y)
{
	return forward_hid_input(from_headset::hid::mouse_move{x, y});
}
bool scenes::stream::on_input_button_down(uint8_t button)
{
	return forward_hid_input(from_headset::hid::button_down{button});
}
bool scenes::stream::on_input_button_up(uint8_t button)
{
	return forward_hid_input(from_headset::hid::button_up{button});
}
bool scenes::stream::on_input_scroll(float h, float v)
{
	return forward_hid_input(from_headset::hid::mouse_scroll{h, v});
}
