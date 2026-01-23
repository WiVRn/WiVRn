/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
 * Copyright (C) 2025  Sapphire <imsapphire0@gmail.com>
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

#include "wivrn_session.h"

#include "accept_connection.h"
#include "application.h"
#include "configuration.h"
#include "driver/app_pacer.h"
#include "main/comp_compositor.h"
#include "main/comp_main_interface.h"
#include "main/comp_target.h"
#include "server/ipc_server.h"
#include "util/u_builders.h"
#include "util/u_logging.h"
#include "util/u_system.h"
#include "utils/load_icon.h"
#include "utils/scoped_lock.h"

#include "audio/audio_setup.h"
#include "wivrn_android_face_tracker.h"
#include "wivrn_comp_target.h"
#include "wivrn_config.h"
#include "wivrn_eye_tracker.h"
#include "wivrn_fb_face2_tracker.h"
#include "wivrn_foveation.h"
#include "wivrn_generic_tracker.h"
#include "wivrn_htc_face_tracker.h"
#include "wivrn_ipc.h"

#include "wivrn_packets.h"
#include "xr/to_string.h"
#include "xrt/xrt_defines.h"
#include "xrt/xrt_device.h"
#include "xrt/xrt_session.h"
#include <algorithm>
#include <chrono>
#include <magic_enum.hpp>
#include <stdexcept>
#include <string.h>
#include <vulkan/vulkan.h>

#if WIVRN_FEATURE_STEAMVR_LIGHTHOUSE
#include "steamvr_lh_interface.h"
#endif

#if WIVRN_FEATURE_SOLARXR
#include "solarxr_interface.h"
#endif

namespace wivrn
{

struct wivrn_comp_target_factory : public comp_target_factory
{
	wivrn_session & session;

	wivrn_comp_target_factory(wivrn_session & session) :
	        comp_target_factory{
	                .name = "WiVRn",
	                .identifier = "wivrn",
	                .requires_vulkan_for_create = false,
	                .is_deferred = false,
	                .required_instance_version = VK_MAKE_VERSION(1, 3, 0),
	                .required_instance_extensions = wivrn_comp_target::wanted_instance_extensions.data(),
	                .required_instance_extension_count = wivrn_comp_target::wanted_instance_extensions.size(),
	                .optional_device_extensions = wivrn_comp_target::wanted_device_extensions.data(),
	                .optional_device_extension_count = wivrn_comp_target::wanted_device_extensions.size(),
	                .detect = wivrn_comp_target_factory::detect,
	                .create_target = wivrn_comp_target_factory::create_target},
	        session(session)
	{
	}

	static bool detect(const struct comp_target_factory * ctf, struct comp_compositor * c)
	{
		return true;
	}

	static bool create_target(const struct comp_target_factory * ctf, struct comp_compositor * c, struct comp_target ** out_ct)
	{
		auto self = (wivrn_comp_target_factory *)ctf;
		self->session.comp_target = new wivrn_comp_target(self->session, c);
		*out_ct = self->session.comp_target;
		return true;
	}
};

bool is_forced_extension(const char * ext_name)
{
	const char * val = std::getenv("WIVRN_FORCE_EXTENSIONS");
	if (not val)
		return false;
	return strstr(val, ext_name);
}

wivrn::wivrn_session::wivrn_session(std::unique_ptr<wivrn_connection> connection, u_system & system) :
        xrt_system_devices{
                .get_roles = [](xrt_system_devices * self, xrt_system_roles * out_roles) { return ((wivrn_session *)self)->get_roles(out_roles); },
                .feature_inc = [](xrt_system_devices * self, xrt_device_feature_type f) { return ((wivrn_session *)self)->feature_inc(f); },
                .feature_dec = [](xrt_system_devices * self, xrt_device_feature_type f) { return ((wivrn_session *)self)->feature_dec(f); },
                .destroy = [](xrt_system_devices * self) { delete ((wivrn_session *)self); },
        },
        connection(std::move(connection)),
        xrt_system(system),
        control(*this->connection),
        hmd(this, get_info()),
        left_controller(XRT_DEVICE_TOUCH_CONTROLLER, 0, &hmd, this),
        right_controller(XRT_DEVICE_TOUCH_CONTROLLER, 1, &hmd, this),
        left_hand_interaction(XRT_DEVICE_EXT_HAND_INTERACTION, 0, &hmd, this),
        right_hand_interaction(XRT_DEVICE_EXT_HAND_INTERACTION, 1, &hmd, this),
        settings(get_info().settings)
{
	try
	{
		audio_handle = audio_device::create(
		        "wivrn.source",
		        "WiVRn(microphone)",
		        "wivrn.sink",
		        "WiVRn",
		        get_info(),
		        *this);
		if (audio_handle)
			send_control(audio_handle->description());
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Failed to register audio device: %s", e.what());
		throw;
	}

	(*this)(from_headset::get_application_list{
	        .language = get_info().language,
	        .country = get_info().country,
	        .variant = get_info().variant,
	});

	static_roles.head = xdevs[xdev_count++] = &hmd;

	if (hmd.supported.face_tracking)
		static_roles.face = &hmd;

	roles.left = left_controller_index = xdev_count++;
	static_roles.hand_tracking.unobstructed.left = xdevs[left_controller_index] = &left_controller;
	xdevs[left_hand_interaction_index = xdev_count++] = &left_hand_interaction;

	roles.right = right_controller_index = xdev_count++;
	static_roles.hand_tracking.unobstructed.right = xdevs[right_controller_index] = &right_controller;
	xdevs[right_hand_interaction_index = xdev_count++] = &right_hand_interaction;

#if WIVRN_FEATURE_STEAMVR_LIGHTHOUSE
	auto use_steamvr_lh = configuration().use_steamvr_lh || std::getenv("WIVRN_USE_STEAMVR_LH");
	xrt_system_devices * lhdevs = NULL;

	if (use_steamvr_lh)
	{
		U_LOG_W("=====================");
		U_LOG_W("Disregard lighthousedb / chaperone related error messages from the lighthouse driver. These are irrelevant in case of WiVRn.");
		U_LOG_W("If getting a SIGSEGV right after this, you are likely using an unsupported SteamVR version!");
		U_LOG_W("=====================");
		if (steamvr_lh_create_devices(nullptr, &lhdevs) == XRT_SUCCESS)
		{
			for (int i = 0; i < lhdevs->xdev_count; i++)
			{
				auto lhdev = lhdevs->xdevs[i];
				switch (lhdev->device_type)
				{
					case XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER:
						roles.left = xdev_count;
						static_roles.hand_tracking.unobstructed.left = nullptr;
						static_roles.hand_tracking.conforming.left = lhdev;
						break;
					case XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER:
						roles.right = xdev_count;
						static_roles.hand_tracking.unobstructed.right = nullptr;
						static_roles.hand_tracking.conforming.right = lhdev;
						break;
					case XRT_DEVICE_TYPE_ANY_HAND_CONTROLLER:
						if (roles.left == left_controller_index)
						{
							roles.left = xdev_count;
							static_roles.hand_tracking.unobstructed.left = nullptr;
							static_roles.hand_tracking.conforming.left = lhdev;
						}
						else if (roles.right == right_controller_index)
						{
							roles.right = xdev_count;
							static_roles.hand_tracking.unobstructed.right = nullptr;
							static_roles.hand_tracking.conforming.right = lhdev;
						}
						break;
					default:
						break;
				}
				xdevs[xdev_count++] = lhdev;
			}
		}
	}
#endif
	if (get_info().eye_gaze || is_forced_extension("EXT_eye_gaze_interaction"))
	{
		eye_tracker = std::make_unique<wivrn_eye_tracker>(&hmd, *this);
		static_roles.eyes = eye_tracker.get();
		xdevs[xdev_count++] = eye_tracker.get();
	}

	auto face = get_info().face_tracking;
	if (face == from_headset::face_type::android || is_forced_extension("ANDROID_face_tracking"))
	{
		android_face_tracker = std::make_unique<wivrn_android_face_tracker>(&hmd, *this);
		static_roles.face = android_face_tracker.get();
		xdevs[xdev_count++] = android_face_tracker.get();
	}
	if (face == from_headset::face_type::fb2 || is_forced_extension("FB_face_tracking2"))
	{
		fb_face2_tracker = std::make_unique<wivrn_fb_face2_tracker>(&hmd, *this);
		static_roles.face = fb_face2_tracker.get();
		xdevs[xdev_count++] = fb_face2_tracker.get();
	}
	if (face == wivrn::from_headset::face_type::htc || is_forced_extension("HTC_facial_tracking"))
	{
		htc_face_tracker = std::make_unique<wivrn_htc_face_tracker>(&hmd, *this);
		static_roles.face = htc_face_tracker.get();
		xdevs[xdev_count++] = htc_face_tracker.get();
	}

	auto num_generic_trackers = get_info().num_generic_trackers;
	generic_trackers.reserve(num_generic_trackers);
	if (num_generic_trackers > 0)
	{
		if (num_generic_trackers > from_headset::body_tracking::max_tracked_poses)
		{
			U_LOG_W("reported generic trackers %d larger than maximum %lu",
			        num_generic_trackers,
			        from_headset::body_tracking::max_tracked_poses);
			num_generic_trackers = from_headset::body_tracking::max_tracked_poses;
		}
		if (num_generic_trackers + xdev_count > std::size(xdevs))
		{
			U_LOG_W("Too many generic trackers: %d, only %lu will be active",
			        num_generic_trackers,
			        std::size(xdevs) - xdev_count);
			num_generic_trackers = std::size(xdevs) - xdev_count;
		}
		U_LOG_I("Creating %d generic trackers", num_generic_trackers);

		for (int i = 0; i < num_generic_trackers; ++i)
		{
			auto dev = std::make_unique<wivrn_generic_tracker>(i, &hmd, *this);
			xdevs[xdev_count++] = dev.get();
			generic_trackers.push_back(std::move(dev));
		}
	}

#if WIVRN_FEATURE_SOLARXR
	uint32_t num_devs = solarxr_device_create_xdevs(static_cast<xrt_device>(hmd).tracking_origin, &xdevs[xdev_count], ARRAY_SIZE(xdevs) - xdev_count);
	if (num_devs != 0)
	{
		static_roles.body = xdevs[xdev_count];
		solarxr_device_set_feeder_devices(static_roles.body, xdevs, xdev_count);
	}
	xdev_count += num_devs;
#endif

	if (roles.left >= 0)
		roles.left_profile = xdevs[roles.left]->name;
	if (roles.right >= 0)
		roles.right_profile = xdevs[roles.right]->name;
	if (roles.gamepad >= 0)
		roles.gamepad_profile = xdevs[roles.gamepad]->name;

	if (auto system_name = get_info().system_name; !system_name.empty())
	{
		system_name += " on WiVRn";
		strlcpy(xrt_system.base.properties.name, system_name.c_str(), std::size(xrt_system.base.properties.name));
	}

	if (configuration().hid_forwarding)
	{
		try
		{
			uinput_handler.emplace();
			send_control(to_headset::feature_control{to_headset::feature_control::hid_input, true});
		}
		catch (...)
		{
			U_LOG_W("Could not initialize keyboard & mouse forwarding");
			U_LOG_W("Ensure that the uinput kernel module is loaded and your user is in the input group.");
			wivrn_ipc_socket_monado->send(from_monado::server_error{
			        .where = "Could not initialize keyboard & mouse forwarding",
			        .message = "Ensure that the uinput kernel module is loaded and your user is in the input group.",
			});
		}
	}
}

wivrn_session::~wivrn_session()
{
#if WIVRN_FEATURE_SOLARXR
	solarxr_device_clear_feeder_devices(static_roles.body);
#endif

	for (size_t i = 0; i < ARRAY_SIZE(xdevs); i++)
	{
		xrt_device_destroy(&xdevs[i]);
	}

	connection->shutdown();
}

xrt_result_t wivrn::wivrn_session::create_session(std::unique_ptr<wivrn_connection> connection,
                                                  u_system & system,
                                                  xrt_system_devices ** out_xsysd,
                                                  xrt_space_overseer ** out_xspovrs,
                                                  xrt_system_compositor ** out_xsysc)
{
	std::unique_ptr<wivrn_session> self;
	try
	{
		self.reset(new wivrn_session(std::move(connection), system));
	}
	catch (std::exception & e)
	{
		U_LOG_E("Error creating WiVRn session: %s", e.what());
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	send_to_main(self->get_info());

	wivrn_comp_target_factory ctf(*self);
	auto xret = comp_main_create_system_compositor(&self->hmd, &ctf, &self->app_pacers, out_xsysc);
	if (xret != XRT_SUCCESS)
	{
		U_LOG_E("Failed to create system compositor");
		return xret;
	}
	self->system_compositor = *out_xsysc;

	u_builder_create_space_overseer_legacy(
	        &self->xrt_system.broadcast,
	        &self->hmd,
	        self->eye_tracker.get(),
	        &self->left_controller,
	        &self->right_controller,
	        nullptr,
	        self->xdevs,
	        self->xdev_count,
	        false,
	        false,
	        out_xspovrs);
	self->space_overseer = *out_xspovrs;

	auto dump_file = std::getenv("WIVRN_DUMP_TIMINGS");
	if (dump_file)
	{
		self->feedback_csv.open(dump_file);
	}

	*out_xsysd = self.release();
	return XRT_SUCCESS;
}

void wivrn_session::start(ipc_server * server)
{
	assert(not net_thread.joinable());
	mnd_ipc_server = server;
	net_thread = std::jthread([this](auto stop_token) { return run_net(stop_token); });
	worker_thread = std::jthread([this](std::stop_token stop) { return run_worker(stop); });
}

void wivrn_session::stop()
{
	net_thread = std::jthread();
	worker_thread = std::jthread();
}

bool wivrn_session::request_stop()
{
	assert(mnd_ipc_server);
	bool b = net_thread.request_stop();
	worker_thread.request_stop();
	ipc_server_stop(mnd_ipc_server);
	return b;
}

clock_offset wivrn_session::get_offset()
{
	return offset_est.get_offset();
}

bool wivrn_session::connected()
{
	return connection->is_active();
}

void wivrn_session::unset_comp_target()
{
	std::lock_guard lock(comp_target_mutex);
	comp_target = nullptr;
}

void wivrn_session::add_tracking_request(device_id device, int64_t at_ns, int64_t produced_ns, int64_t now)
{
	control.add_request(device, now, at_ns, produced_ns);
}

void wivrn_session::add_tracking_request(device_id device, int64_t at_ns, int64_t produced_ns)
{
	control.add_request(device, os_monotonic_get_ns(), at_ns, produced_ns);
}

void wivrn_session::operator()(from_headset::headset_info_packet &&)
{
	U_LOG_W("unexpected headset info packet, ignoring");
}

void wivrn_session::operator()(const from_headset::settings_changed & settings)
{
	*this->settings.lock() = settings;

	if (settings.bitrate_bps != 0)
	{
		std::lock_guard lock(comp_target_mutex);
		if (comp_target)
			comp_target->set_bitrate(settings.bitrate_bps);
	}

	wivrn_ipc_socket_monado->send(std::move(settings));
}

static xrt_device_name get_name(interaction_profile profile)
{
	switch (profile)
	{
		case interaction_profile::none:
			return XRT_DEVICE_INVALID;
		case interaction_profile::khr_simple_controller:
			return XRT_DEVICE_SIMPLE_CONTROLLER;
		case interaction_profile::ext_hand_interaction_ext:
			return XRT_DEVICE_EXT_HAND_INTERACTION;
		case interaction_profile::bytedance_pico_neo3_controller:
			return XRT_DEVICE_PICO_NEO3_CONTROLLER;
		case interaction_profile::bytedance_pico4_controller:
		case interaction_profile::bytedance_pico4s_controller:
			return XRT_DEVICE_PICO4_CONTROLLER;
		case interaction_profile::bytedance_pico_g3_controller:
			return XRT_DEVICE_PICO_G3_CONTROLLER;
		case interaction_profile::google_daydream_controller:
			return XRT_DEVICE_DAYDREAM;
		case interaction_profile::hp_mixed_reality_controller:
		case interaction_profile::microsoft_motion_controller:
			return XRT_DEVICE_WMR_CONTROLLER;
		case interaction_profile::htc_vive_controller:
			return XRT_DEVICE_VIVE_WAND;
		case interaction_profile::htc_vive_cosmos_controller:
			return XRT_DEVICE_VIVE_COSMOS_CONTROLLER;
		case interaction_profile::htc_vive_focus3_controller:
			return XRT_DEVICE_VIVE_FOCUS3_CONTROLLER;
		case interaction_profile::htc_vive_pro:
			return XRT_DEVICE_VIVE_PRO;
		case interaction_profile::ml_ml2_controller:
			return XRT_DEVICE_ML2_CONTROLLER;
		case interaction_profile::microsoft_xbox_controller:
			return XRT_DEVICE_XBOX_CONTROLLER;
		case interaction_profile::oculus_go_controller:
			return XRT_DEVICE_GO_CONTROLLER;
		case interaction_profile::oculus_touch_controller:
		case interaction_profile::meta_touch_controller_rift_cv1:
		case interaction_profile::meta_touch_controller_quest_1_rift_s:
		case interaction_profile::meta_touch_controller_quest_2:
			return XRT_DEVICE_TOUCH_CONTROLLER;
		case interaction_profile::meta_touch_pro_controller:
			return XRT_DEVICE_TOUCH_PRO_CONTROLLER;
		case interaction_profile::meta_touch_plus_controller:
			return XRT_DEVICE_TOUCH_PLUS_CONTROLLER;
		case interaction_profile::samsung_odyssey_controller:
			return XRT_DEVICE_SAMSUNG_ODYSSEY_CONTROLLER;
		case interaction_profile::valve_index_controller:
			return XRT_DEVICE_INDEX_CONTROLLER;
	}
	throw std::runtime_error("invalid interaction profile id " + std::to_string(int(profile)));
}

void wivrn_session::operator()(const from_headset::tracking & tracking)
{
	auto left = (roles.left == -1 || roles.left == left_controller_index || roles.left == left_hand_interaction_index) ? get_name(tracking.interaction_profiles[0]) : XRT_DEVICE_INVALID;
	auto right = (roles.right == -1 || roles.right == right_controller_index || roles.right == right_hand_interaction_index) ? get_name(tracking.interaction_profiles[1]) : XRT_DEVICE_INVALID;
	if (left != roles.left_profile or right != roles.right_profile)
	{
		U_LOG_I("Updating interaction profiles: from \n"
		        "\t%s (left)  to %s\n"
		        "\t%s (right) to %s\n",
		        std::string(magic_enum::enum_name(roles.left_profile)).c_str(),
		        std::string(magic_enum::enum_name(left)).c_str(),
		        std::string(magic_enum::enum_name(roles.right_profile)).c_str(),
		        std::string(magic_enum::enum_name(right)).c_str());
		std::lock_guard lock(roles_mutex);

		// don't change role when hand from other driver is used
		if (roles.left == -1 || roles.left == left_hand_interaction_index || roles.left == left_controller_index)
		{
			if (left == XRT_DEVICE_EXT_HAND_INTERACTION)
			{
				left_hand_interaction.reset_history();
				roles.left = left_hand_interaction_index;
			}
			else if (left != XRT_DEVICE_INVALID)
			{
				left_controller.reset_history();
				roles.left = left_controller_index;
			}
			else
			{
				roles.left = -1;
			}
		}
		roles.left_profile = left;

		if (roles.right == -1 || roles.right == right_hand_interaction_index || roles.right == right_controller_index)
		{
			if (right == XRT_DEVICE_EXT_HAND_INTERACTION)
			{
				right_hand_interaction.reset_history();
				roles.right = right_hand_interaction_index;
			}
			else if (right != XRT_DEVICE_INVALID)
			{
				right_controller.reset_history();
				roles.right = right_controller_index;
			}
			else
			{
				roles.right = -1;
			}
		}
		roles.right_profile = right;

		++roles.generation_id;
	}
	if (tracking.state_flags & from_headset::tracking::state_flags::recentered)
	{
		U_LOG_I("recentering requested");
		if (XRT_SUCCESS != xrt_space_overseer_recenter_local_spaces(space_overseer))
			U_LOG_W("failed to recenter local spaces");
	}

	auto offset = offset_est.get_offset();

	if (offset)
	{
		XrDuration latency = os_monotonic_get_ns() - offset.from_headset(tracking.production_timestamp);
		tracking_latency = std::lerp(tracking_latency.load(), latency, 0.1);
	}

	hmd.update_tracking(tracking, offset);
	if (roles.left == left_hand_interaction_index)
		left_hand_interaction.update_tracking(tracking, offset);
	else
		left_controller.update_tracking(tracking, offset);

	if (roles.right == right_hand_interaction_index)
		right_hand_interaction.update_tracking(tracking, offset);
	else
		right_controller.update_tracking(tracking, offset);

	if (eye_tracker)
		eye_tracker->update_tracking(tracking, offset);
	{
		std::shared_lock lock(comp_target_mutex);
		if (comp_target)
			comp_target->foveation->update_tracking(tracking, offset);
	}

	if (android_face_tracker)
		android_face_tracker->update_tracking(tracking, offset);
	else if (fb_face2_tracker)
		fb_face2_tracker->update_tracking(tracking, offset);
	else if (htc_face_tracker)
		htc_face_tracker->update_tracking(tracking, offset);
}

void wivrn_session::operator()(from_headset::override_foveation_center && foveation_center)
{
	std::shared_lock lock(comp_target_mutex);
	if (comp_target)
		comp_target->foveation->update_foveation_center_override(foveation_center);
}

void wivrn_session::operator()(from_headset::derived_pose && derived)
{
	left_controller.set_derived_pose(derived);
	left_hand_interaction.set_derived_pose(derived);

	right_controller.set_derived_pose(derived);
	right_hand_interaction.set_derived_pose(derived);
}

void wivrn_session::operator()(from_headset::hand_tracking && hand_tracking)
{
	auto offset = offset_est.get_offset();

	left_controller.update_hand_tracking(hand_tracking, offset);
	right_controller.update_hand_tracking(hand_tracking, offset);
}
void wivrn_session::operator()(from_headset::body_tracking && body_tracking)
{
	auto offset = offset_est.get_offset();

	assert(generic_trackers.size() <= from_headset::body_tracking::max_tracked_poses);
	for (int i = 0; i < generic_trackers.size(); i++)
	{
		auto pose = body_tracking.poses ? (*body_tracking.poses)[i] : from_headset::body_tracking::pose{};
		generic_trackers[i]->update_tracking(body_tracking, pose, offset);
	}
}
void wivrn_session::operator()(from_headset::inputs && inputs)
{
	auto offset = get_offset();

	if (roles.left == left_hand_interaction_index)
		left_hand_interaction.set_inputs(inputs, offset);
	else if (roles.left == left_controller_index)
		left_controller.set_inputs(inputs, offset);

	if (roles.right == right_hand_interaction_index)
		right_hand_interaction.set_inputs(inputs, offset);
	else if (roles.right == right_controller_index)
		right_controller.set_inputs(inputs, offset);
}

void wivrn_session::operator()(from_headset::hid::input && e)
{
	try
	{
		if (uinput_handler)
			uinput_handler->handle_input(e);
	}
	catch (const std::exception & e)
	{
		wivrn_ipc_socket_monado->send(from_monado::server_error{
		        .where = "HID forwarding error",
		        .message = e.what(),
		});
		U_LOG_E("HID forwarding error: %s", e.what());
		uinput_handler.reset();
	}
}

void wivrn_session::operator()(from_headset::timesync_response && timesync)
{
	offset_est.add_sample(timesync);
}

void wivrn_session::operator()(from_headset::feedback && feedback)
{
	clock_offset o = offset_est.get_offset();
	if (not o)
		return;
	{
		std::shared_lock lock(comp_target_mutex);
		if (comp_target)
			comp_target->on_feedback(feedback, o);
	}

	if (feedback.received_first_packet)
		dump_time("receive_begin", feedback.frame_index, o.from_headset(feedback.received_first_packet), feedback.stream_index);
	if (feedback.received_last_packet)
		dump_time("receive_end", feedback.frame_index, o.from_headset(feedback.received_last_packet), feedback.stream_index);
	if (feedback.sent_to_decoder)
		dump_time("decode_begin", feedback.frame_index, o.from_headset(feedback.sent_to_decoder), feedback.stream_index);
	if (feedback.received_from_decoder)
		dump_time("decode_end", feedback.frame_index, o.from_headset(feedback.received_from_decoder), feedback.stream_index);
	if (feedback.blitted)
		dump_time("blit", feedback.frame_index, o.from_headset(feedback.blitted), feedback.stream_index);
	if (feedback.displayed)
		dump_time("display", feedback.frame_index, o.from_headset(feedback.displayed), feedback.stream_index);
}

void wivrn_session::operator()(from_headset::battery && battery)
{
	hmd.update_battery(battery);
}

void wivrn_session::operator()(from_headset::visibility_mask_changed && mask)
{
	hmd.update_visibility_mask(mask);
	xrt_session_event event{
	        .mask_change = {
	                .type = XRT_SESSION_EVENT_VISIBILITY_MASK_CHANGE,
	                .view_index = mask.view_index,
	        },
	};
	auto result = xrt_session_event_sink_push(&xrt_system.broadcast, &event);
}

void wivrn_session::operator()(from_headset::session_state_changed && event)
{
	assert(mnd_ipc_server);
	U_LOG_I("Session state changed: %s", xr::to_string(event.state));
	bool visible, focused;
	switch (event.state)
	{
		case XR_SESSION_STATE_VISIBLE:
			visible = true;
			focused = false;
			break;
		case XR_SESSION_STATE_FOCUSED:
			visible = true;
			focused = true;
			break;
		default:
			visible = false;
			focused = false;
			break;
	}
	scoped_lock lock(mnd_ipc_server->global_state.lock);
	auto locked = session_loss.lock();
	for (auto & t: mnd_ipc_server->threads)
	{
		auto id = t.ics.client_state.id;
		if (t.ics.server_thread_index < 0 or t.ics.xc == nullptr or locked->contains(id))
			continue;
		bool current = t.ics.client_state.session_overlay or
		               mnd_ipc_server->global_state.active_client_index == t.ics.server_thread_index;
		U_LOG_D("Setting session state for app %s: visible=%s focused=%s current=%s",
		        t.ics.client_state.info.application_name,
		        visible ? "true" : "false",
		        focused ? "true" : "false",
		        current ? "true" : "false");
		xrt_syscomp_set_state(system_compositor, t.ics.xc, visible and current, focused and current, os_monotonic_get_ns());
	}
}
void wivrn_session::operator()(from_headset::user_presence_changed && event)
{
	if (hmd.update_presence(event.present))
		push_event(
		        {
		                .presence_change = {
		                        .type = XRT_SESSION_EVENT_USER_PRESENCE_CHANGE,
		                        .is_user_present = event.present,
		                },
		        });
}

void wivrn_session::operator()(from_headset::refresh_rate_changed && event)
{
	{
		std::shared_lock lock(comp_target_mutex);
		if (comp_target)
			comp_target->set_refresh_rate(event.to);
	}
	push_event(
	        {
	                .display = {
	                        .type = XRT_SESSION_EVENT_DISPLAY_REFRESH_RATE_CHANGE,
	                        .from_display_refresh_rate_hz = event.from,
	                        .to_display_refresh_rate_hz = event.to,
	                },
	        });
}

void wivrn_session::operator()(from_headset::get_application_list && request)
{
	to_headset::application_list response{
	        .language = std::move(request.language),
	        .country = std::move(request.country),
	        .variant = std::move(request.variant),
	};

	auto apps = list_applications();

	for (const auto & [id, app]: apps)
	{
		response.applications.emplace_back(
		        std::move(id),
		        // FIXME: use locale
		        app.name.at(""));
	}
	send_control(std::move(response));

	for (const auto & [id, app]: apps)
	{
		if (app.icon_path)
		{
			try
			{
				const auto & icons = load_icon(*app.icon_path);

				if (icons.empty())
					continue;

				const auto & largest_icon = std::ranges::max(icons, [](const wivrn::icon & a, const wivrn::icon & b) {
					if (a.bpp < b.bpp)
						return true;
					if (a.bpp > b.bpp)
						return false;
					return a.width * a.height < b.width * b.height;
				});

				send_control(to_headset::application_icon{
				        .id = id,
				        .image = largest_icon.png_data,
				});
			}
			catch (std::exception & e)
			{
				U_LOG_W("Error loading icon %s: %s", app.icon_path->c_str(), e.what());
			}
		}
	}
}

void wivrn_session::operator()(const from_headset::start_app & request)
{
	send_to_main(request);
}

void wivrn_session::operator()(const from_headset::get_running_applications &)
{
	assert(mnd_ipc_server);
	scoped_lock lock(mnd_ipc_server->global_state.lock);
	to_headset::running_applications msg{};
	for (auto & t: mnd_ipc_server->threads)
	{
		if (t.ics.server_thread_index < 0 or t.ics.xc == nullptr)
			continue;
		// nasty volatile
		std::array<char, sizeof(t.ics.client_state.info.application_name)> tmp;
		for (size_t i = 0; i + 1 < tmp.size(); ++i)
			tmp[i] = t.ics.client_state.info.application_name[i];
		tmp.back() = 0;
		msg.applications.push_back(
		        {
		                .name = std::string(tmp.data()),
		                .id = t.ics.client_state.id,
		                .overlay = t.ics.client_state.session_overlay,
		                .active = t.ics.server_thread_index == mnd_ipc_server->global_state.active_client_index,
		        });
	}
	connection->send_control(std::move(msg));
}

void wivrn_session::operator()(const from_headset::set_active_application & req)
{
	assert(mnd_ipc_server);
	ipc_server_set_active_client(mnd_ipc_server, req.id);
	ipc_server_update_state(mnd_ipc_server);
	// Send a refreshed application list
	(*this)(from_headset::get_running_applications{});
}

void wivrn_session::operator()(const from_headset::stop_application & req)
{
	assert(mnd_ipc_server);
	scoped_lock lock(mnd_ipc_server->global_state.lock);
	for (auto & t: mnd_ipc_server->threads)
	{
		if (t.ics.client_state.id == req.id)
		{
			if (!t.ics.xs)
			{
				U_LOG_W("Unable to stop app %s: no session!", t.ics.client_state.info.application_name);
				break;
			}

			U_LOG_I("Request exit for application %s", t.ics.client_state.info.application_name);
			if (xrt_session_request_exit(t.ics.xs) != XRT_SUCCESS)
				U_LOG_W("Failed to request exit for application %s", t.ics.client_state.info.application_name);

			auto when = os_monotonic_get_ns() + 10l * U_TIME_1S_IN_NS;
			session_loss.lock()->emplace(req.id, when);
			break;
		}
	}
}

void wivrn_session::operator()(audio_data && data)
{
	if (audio_handle)
		audio_handle->process_mic_data(std::move(data));
}

void wivrn_session::operator()(to_monado::stop &&)
{
	request_stop();
}

void wivrn_session::operator()(to_monado::disconnect &&)
{
	connection->shutdown();
	throw std::runtime_error("Disconnecting as requested by main loop");
}

void wivrn_session::operator()(to_monado::set_bitrate && data)
{
	std::shared_lock lock(comp_target_mutex);
	if (comp_target)
		comp_target->set_bitrate(data.bitrate_bps);
}

struct refresh_rate_adjuster
{
	std::chrono::seconds period{10};
	std::chrono::steady_clock::time_point next = std::chrono::steady_clock::now() + period;
	pacing_app_factory & pacers;
	const from_headset::headset_info_packet & info;
	thread_safe<from_headset::settings_changed> & settings;
	float last = 0;

	refresh_rate_adjuster(const from_headset::headset_info_packet & info, thread_safe<from_headset::settings_changed> & settings, pacing_app_factory & pacers) :
	        pacers(pacers),
	        info(info),
	        settings(settings)
	{}

	bool advance(std::chrono::steady_clock::time_point now)
	{
		if (next > now)
			return false;
		next += period;
		return true;
	}

	void adjust(wivrn_connection & cnx)
	{
		auto locked = settings.lock();
		if (locked->preferred_refresh_rate != 0 or info.available_refresh_rates.size() < 2)
			return;

		// Maximum refresh rate the application can reach
		auto app_rate = float(U_TIME_1S_IN_NS) / pacers.get_frame_time();
		// Get the highest rate reachable by the application
		// If none can be reached, set it to the maximum
		auto requested = info.available_refresh_rates.back();
		for (auto rate: info.available_refresh_rates)
		{
			if (rate > locked->minimum_refresh_rate and rate < app_rate * (rate == last ? 1. : 0.9))
				requested = rate;
		}
		if (requested != last)
		{
			U_LOG_I("requesting refresh rate: %.0f (app rate %.1f)", requested, app_rate);
			cnx.send_control(to_headset::refresh_rate_change{.fps = requested});
			last = requested;
		}
	}

	void reset()
	{
		last = 0;
	}
};

void wivrn_session::run_net(std::stop_token stop)
{
	while (not stop.stop_requested())
	{
		try
		{
			connection->poll(*this, 20);
		}
		catch (const std::exception & e)
		{
			U_LOG_E("Exception in network thread: %s", e.what());
			worker_thread = std::jthread();
			reconnect(stop);
			worker_thread = std::jthread([this](std::stop_token stop) { return run_worker(stop); });
		}
	}
}

void wivrn_session::run_worker(std::stop_token stop)
{
	refresh_rate_adjuster refresh(connection->info(), settings, app_pacers);
	while (not stop.stop_requested())
	{
		try
		{
			std::this_thread::sleep_until(std::min(
			        {
			                refresh.next,
			                control.next,
			                offset_est.next(),
			        }));
			auto now = std::chrono::steady_clock::now();
			offset_est.request_sample(now, *connection);
			const bool do_refresh = refresh.advance(now);
			const bool do_control = control.advance(now);
			if (do_refresh or do_control)
			{
				std::shared_lock lock(comp_target_mutex);
				if (comp_target)
				{
					if (do_refresh)
					{
						{
							scoped_lock lock(xrt_system.sessions.mutex);
							if (xrt_system.sessions.count == 0)
								comp_target->requested_refresh_rate = 0;
						}
						if (comp_target->requested_refresh_rate == 0)
							refresh.adjust(*connection);
					}
					if (do_control)
						control.resolve(comp_target->pacer.get_frame_duration(), tracking_latency);
				}
			}
			poll_session_loss();
		}
		catch (const std::exception & e)
		{
			U_LOG_E("Exception in worker thread: %s", e.what());
		}
	}
}

xrt_result_t wivrn_session::push_event(const xrt_session_event & event)
{
	return xrt_session_event_sink_push(&xrt_system.broadcast, &event);
}

void wivrn_session::set_foveated_size(uint32_t width, uint32_t height)
{
	hmd.set_foveated_size(width, height);
}

void wivrn_session::dump_time(const std::string & event, uint64_t frame, int64_t time, uint8_t stream, const char * extra)
{
	if (feedback_csv)
	{
		std::lock_guard lock(csv_mutex);
		feedback_csv << std::quoted(event) << "," << frame << "," << time << "," << (int)stream << extra << std::endl;
	}
}

void wivrn_session::quit_if_no_client()
{
	scoped_lock lock(xrt_system.sessions.mutex);
	if (xrt_system.sessions.count == 0)
	{
		U_LOG_I("No OpenXR client connected, exiting");
		request_stop();
	}
}

void wivrn_session::reconnect(std::stop_token stop)
{
	assert(mnd_ipc_server);
	// Notify clients about disconnected status
	xrt_session_event event{
	        .state = {
	                .type = XRT_SESSION_EVENT_STATE_CHANGE,
	                .visible = false,
	                .focused = false,
	        },
	};
	auto result = push_event(event);
	if (result != XRT_SUCCESS)
	{
		U_LOG_W("Failed to notify session state change");
	}

	U_LOG_I("Waiting for new connection");
	auto tcp = accept_connection(*this, stop, &wivrn_session::quit_if_no_client);
	if (stop.stop_requested())
		return;
	if (not tcp)
	{
		request_stop();
		return;
	}
	try
	{
		offset_est.reset();
		connection->reset(stop, std::move(*tcp), [this]() { quit_if_no_client(); });

		const auto & info = connection->info();
		(*this)(info.settings);

		{
			std::shared_lock lock(comp_target_mutex);
			if (comp_target)
				comp_target->reset_encoders();
		}
		if (audio_handle)
			send_control(audio_handle->description());

		event.state.visible = true;
		event.state.focused = true;
		result = push_event(event);
		if (result != XRT_SUCCESS)
		{
			U_LOG_W("Failed to notify session state change");
		}
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Reconnection failed: %s", e.what());
	}
}

void wivrn_session::poll_session_loss()
{
	assert(mnd_ipc_server);
	scoped_lock lock(mnd_ipc_server->global_state.lock);
	auto locked = session_loss.lock();
	auto now = os_monotonic_get_ns();
	if (locked->empty())
		return;
	auto it = locked->begin();
	while (it != locked->end() and it->second <= now)
	{
		for (auto & t: mnd_ipc_server->threads)
		{
			if (t.ics.client_state.id == it->first)
			{
				U_LOG_I("Terminating %s", t.ics.client_state.info.application_name);
				xrt_syscomp_notify_lost(system_compositor, t.ics.xc);
				break;
			}
		}
		it = locked->erase(it);
	}
}

xrt_result_t wivrn_session::get_roles(xrt_system_roles * out_roles)
{
	std::lock_guard lock(roles_mutex);
	*out_roles = roles;
	return XRT_SUCCESS;
}

xrt_result_t wivrn_session::feature_inc(xrt_device_feature_type type)
{
	switch (type)
	{
		case XRT_DEVICE_FEATURE_HAND_TRACKING_LEFT:
		case XRT_DEVICE_FEATURE_HAND_TRACKING_RIGHT:
		case XRT_DEVICE_FEATURE_EYE_TRACKING:
		case XRT_DEVICE_FEATURE_FACE_TRACKING:
			return XRT_SUCCESS;
		default:
			return XRT_ERROR_FEATURE_NOT_SUPPORTED;
	}
}

xrt_result_t wivrn_session::feature_dec(xrt_device_feature_type type)
{
	switch (type)
	{
		case XRT_DEVICE_FEATURE_HAND_TRACKING_LEFT:
		case XRT_DEVICE_FEATURE_HAND_TRACKING_RIGHT:
		case XRT_DEVICE_FEATURE_EYE_TRACKING:
		case XRT_DEVICE_FEATURE_FACE_TRACKING:
			return XRT_SUCCESS;
		default:
			return XRT_ERROR_FEATURE_NOT_SUPPORTED;
	}
}
} // namespace wivrn
