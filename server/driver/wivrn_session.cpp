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

#include "wivrn_session.h"

#include "accept_connection.h"
#include "main/comp_compositor.h"
#include "main/comp_main_interface.h"
#include "main/comp_target.h"
#include "util/u_builders.h"
#include "util/u_logging.h"
#include "util/u_system.h"
#include "utils/scoped_lock.h"

#include "audio/audio_setup.h"
#include "wivrn_comp_target.h"
#include "wivrn_config.h"
#include "wivrn_eye_tracker.h"
#include "wivrn_fb_face2_tracker.h"
#include "wivrn_foveation.h"
#include "wivrn_ipc.h"

#include "xrt/xrt_session.h"
#include <cmath>
#include <magic_enum.hpp>
#include <stdexcept>
#include <vulkan/vulkan.h>

#if WIVRN_FEATURE_STEAMVR_LIGHTHOUSE
#include "steamvr_lh_interface.h"
#endif

#if WIVRN_FEATURE_SOLARXR
#include "solarxr_device.h"
#endif

namespace wivrn
{

struct wivrn_comp_target_factory : public comp_target_factory
{
	wivrn_session & session;
	float fps;

	wivrn_comp_target_factory(wivrn_session & session, float fps) :
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
	        session(session),
	        fps(fps)
	{
	}

	static bool detect(const struct comp_target_factory * ctf, struct comp_compositor * c)
	{
		return true;
	}

	static bool create_target(const struct comp_target_factory * ctf, struct comp_compositor * c, struct comp_target ** out_ct)
	{
		auto self = (wivrn_comp_target_factory *)ctf;
		self->session.comp_target = new wivrn_comp_target(self->session, c, self->fps);
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

void wivrn::tracking_control_t::send(wivrn_connection & connection, bool now)
{
	std::lock_guard lock(mutex);
	if (std::chrono::steady_clock::now() < next_sample and not now)
		return;

	connection.send_stream(to_headset::tracking_control{
	        .offset = std::chrono::nanoseconds(max.exchange(0)),
	        .enabled = enabled,
	});
	if (not now)
		next_sample += std::chrono::seconds(1);
}

bool wivrn::tracking_control_t::set_enabled(to_headset::tracking_control::id id, bool enabled)
{
	std::lock_guard lock(mutex);
	bool changed = enabled != this->enabled[size_t(id)];
	if (changed)
		U_LOG_I("%s tracking: %s", std::string(magic_enum::enum_name(id)).c_str(), enabled ? "enabled" : "disabled");
	this->enabled[size_t(id)] = enabled;
	return changed;
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
        hmd(this, get_info()),
        left_hand(0, &hmd, this),
        right_hand(1, &hmd, this)
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

	static_roles.head = xdevs[xdev_count++] = &hmd;

	if (hmd.face_tracking_supported)
		static_roles.face = &hmd;

	roles.left = xdev_count;
	static_roles.hand_tracking.left = xdevs[xdev_count++] = &left_hand;

	roles.right = xdev_count;
	static_roles.hand_tracking.right = xdevs[xdev_count++] = &right_hand;

#if WIVRN_FEATURE_STEAMVR_LIGHTHOUSE
	auto use_steamvr_lh = std::getenv("WIVRN_USE_STEAMVR_LH");
	xrt_system_devices * lhdevs = NULL;

	if (use_steamvr_lh && steamvr_lh_create_devices(&lhdevs) == XRT_SUCCESS)
	{
		for (int i = 0; i < lhdevs->xdev_count; i++)
		{
			auto lhdev = lhdevs->xdevs[i];
			switch (lhdev->device_type)
			{
				case XRT_DEVICE_TYPE_LEFT_HAND_CONTROLLER:
					roles.left = xdev_count;
					static_roles.hand_tracking.left = lhdev;
					break;
				case XRT_DEVICE_TYPE_RIGHT_HAND_CONTROLLER:
					roles.right = xdev_count;
					static_roles.hand_tracking.right = lhdev;
					break;
				default:
					break;
			}
			xdevs[xdev_count++] = lhdev;
		}
	}
#endif
	if (get_info().eye_gaze || is_forced_extension("EXT_eye_gaze_interaction"))
	{
		eye_tracker = std::make_unique<wivrn_eye_tracker>(&hmd);
		foveation = std::make_unique<wivrn_foveation>();
		static_roles.eyes = eye_tracker.get();
		xdevs[xdev_count++] = eye_tracker.get();
	}
	if (get_info().face_tracking2_fb || is_forced_extension("FB_face_tracking2"))
	{
		fb_face2_tracker = std::make_unique<wivrn_fb_face2_tracker>(&hmd, *this);
		static_roles.face = fb_face2_tracker.get();
		xdevs[xdev_count++] = fb_face2_tracker.get();
	}

#if WIVRN_FEATURE_SOLARXR
	xrt_device * solar_devs[XRT_SYSTEM_MAX_DEVICES];
	uint32_t solar_devs_cap = XRT_SYSTEM_MAX_DEVICES - xdev_count;
	uint32_t num_devs = solarxr_device_create_xdevs(&hmd, solar_devs, XRT_SYSTEM_MAX_DEVICES - xdev_count);
	for (int i = 0; i < num_devs; i++)
	{
		xdevs[xdev_count++] = solar_devs[i];
		if (i == 0)
			static_roles.body = solar_devs[i];
	}
#endif

	if (roles.left >= 0)
		roles.left_profile = xdevs[roles.left]->name;
	if (roles.right >= 0)
		roles.right_profile = xdevs[roles.right]->name;
	if (roles.gamepad >= 0)
		roles.gamepad_profile = xdevs[roles.gamepad]->name;
}

wivrn_session::~wivrn_session()
{
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

	wivrn_comp_target_factory ctf(*self, self->get_info().preferred_refresh_rate);
	auto xret = comp_main_create_system_compositor(&self->hmd, &ctf, out_xsysc);
	if (xret != XRT_SUCCESS)
	{
		U_LOG_E("Failed to create system compositor");
		return xret;
	}

	u_builder_create_space_overseer_legacy(
	        &self->xrt_system.broadcast,
	        &self->hmd,
	        &self->left_hand,
	        &self->right_hand,
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

	self->thread = std::jthread(&wivrn_session::run, self.get());
	*out_xsysd = self.release();
	return XRT_SUCCESS;
}

clock_offset wivrn_session::get_offset()
{
	return offset_est.get_offset();
}

bool wivrn_session::connected()
{
	return connection->is_active();
}

void wivrn_session::operator()(from_headset::headset_info_packet &&)
{
	U_LOG_W("unexpected headset info packet, ignoring");
}
void wivrn_session::operator()(from_headset::trackings && tracking)
{
	for (auto & item: tracking.items)
		(*this)(item);
}
void wivrn_session::operator()(const from_headset::tracking & tracking)
{
	if (tracking.state_flags & from_headset::tracking::state_flags::recentered)
	{
		U_LOG_I("recentering requested");
		if (XRT_SUCCESS != xrt_space_overseer_recenter_local_spaces(space_overseer))
			U_LOG_W("failed to recenter local spaces");
	}

	auto offset = offset_est.get_offset();

	hmd.update_tracking(tracking, offset);
	left_hand.update_tracking(tracking, offset);
	right_hand.update_tracking(tracking, offset);
	if (eye_tracker)
		eye_tracker->update_tracking(tracking, offset);
	if (foveation)
		foveation->update_tracking(tracking, offset);
	if (fb_face2_tracker)
		fb_face2_tracker->update_tracking(tracking, offset);
}

void wivrn_session::operator()(from_headset::hand_tracking && hand_tracking)
{
	auto offset = offset_est.get_offset();

	left_hand.update_hand_tracking(hand_tracking, offset);
	right_hand.update_hand_tracking(hand_tracking, offset);
}
void wivrn_session::operator()(from_headset::inputs && inputs)
{
	auto offset = get_offset();

	left_hand.set_inputs(inputs, offset);
	right_hand.set_inputs(inputs, offset);
}

void wivrn_session::operator()(from_headset::timesync_response && timesync)
{
	offset_est.add_sample(timesync);
}

static auto to_tracking_control(device_id id)
{
	using tid = to_headset::tracking_control::id;
	switch (id)
	{
		case device_id::LEFT_AIM:
			return tid::left_aim;
		case device_id::LEFT_GRIP:
			return tid::left_grip;
		case device_id::LEFT_PALM:
			return tid::left_palm;
		case device_id::RIGHT_AIM:
			return tid::right_aim;
		case device_id::RIGHT_GRIP:
			return tid::right_grip;
		case device_id::RIGHT_PALM:
			return tid::right_palm;
		default:
			break;
	}
	__builtin_unreachable();
}

void wivrn_session::set_enabled(to_headset::tracking_control::id id, bool enabled)
{
	tracking_control.set_enabled(id, enabled);
}

void wivrn_session::set_enabled(device_id id, bool enabled)
{
	if (tracking_control.set_enabled(to_tracking_control(id), enabled) and enabled)
		tracking_control.send(*connection, true);
}

void wivrn_session::operator()(from_headset::feedback && feedback)
{
	assert(comp_target);
	clock_offset o = offset_est.get_offset();
	if (not o)
		return;
	comp_target->on_feedback(feedback, o);

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

void wivrn_session::operator()(audio_data && data)
{
	if (audio_handle)
		audio_handle->process_mic_data(std::move(data));
}

void wivrn_session::operator()(to_monado::disconnect &&)
{
	throw std::runtime_error("Disconnecting as requested by main loop");
}

void wivrn_session::run(std::stop_token stop)
{
	while (not stop.stop_requested())
	{
		try
		{
			offset_est.request_sample(*connection);
			tracking_control.send(*connection);
			connection->poll(*this, 20);
		}
		catch (const std::exception & e)
		{
			U_LOG_E("Exception in network thread: %s", e.what());
			reconnect();
		}
	}
}

std::array<to_headset::foveation_parameter, 2> wivrn_session::set_foveated_size(uint32_t width, uint32_t height)
{
	auto p = hmd.set_foveated_size(width, height);

	if (foveation)
		foveation->set_initial_parameters(p);

	return p;
}

bool wivrn_session::apply_dynamic_foveation()
{
	if (!foveation)
		return false;

	hmd.set_foveation_center(foveation->get_center());
	comp_target->render_dynamic_foveation(hmd.get_foveation_parameters());
	return true;
}

std::array<to_headset::foveation_parameter, 2> wivrn_session::get_foveation_parameters()
{
	return hmd.get_foveation_parameters();
}

void wivrn_session::dump_time(const std::string & event, uint64_t frame, int64_t time, uint8_t stream, const char * extra)
{
	if (feedback_csv)
	{
		std::lock_guard lock(csv_mutex);
		feedback_csv << std::quoted(event) << "," << frame << "," << time << "," << (int)stream << extra << std::endl;
	}
}

static bool quit_if_no_client(u_system & xrt_system)
{
	scoped_lock lock(xrt_system.sessions.mutex);
	if (xrt_system.sessions.count == 0)
	{
		U_LOG_I("No OpenXR client connected, exiting");
		exit(0);
	}
	return false;
}

void wivrn_session::reconnect()
{
	// Notify clients about disconnected status
	xrt_session_event event{
	        .state = {
	                .type = XRT_SESSION_EVENT_STATE_CHANGE,
	                .visible = false,
	                .focused = false,
	        },
	};
	auto result = xrt_session_event_sink_push(&xrt_system.broadcast, &event);
	if (result != XRT_SUCCESS)
	{
		U_LOG_W("Failed to notify session state change");
	}

	U_LOG_I("Waiting for new connection");
	auto tcp = accept_connection(0 /*stdin*/, [this]() { return quit_if_no_client(xrt_system); });
	if (not tcp)
		exit(0);

	struct no_client_connected
	{};

	try
	{
		offset_est.reset();
		connection->reset(std::move(*tcp), [this]() {
			if (quit_if_no_client(xrt_system))
				throw no_client_connected{};
		});

		// const auto & info = connection->info();
		// FIXME: ensure new client is compatible

		comp_target->reset_encoders();
		if (audio_handle)
			send_control(audio_handle->description());

		event.state.visible = true;
		event.state.focused = true;
		result = xrt_session_event_sink_push(&xrt_system.broadcast, &event);
		if (result != XRT_SUCCESS)
		{
			U_LOG_W("Failed to notify session state change");
		}
	}
	catch (no_client_connected)
	{
		U_LOG_I("No OpenXR application connected");
		exit(0);
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Reconnection failed: %s", e.what());
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
			return XRT_SUCCESS;
		default:
			return XRT_ERROR_FEATURE_NOT_SUPPORTED;
	}
}
} // namespace wivrn
