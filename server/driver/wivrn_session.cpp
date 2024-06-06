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
#include "main/comp_main_interface.h"
#include "main/comp_target.h"
#include "util/u_builders.h"
#include "util/u_logging.h"
#include "util/u_system.h"
#include "util/u_system_helpers.h"
#include "utils/scoped_lock.h"

#include "audio/audio_setup.h"
#include "wivrn_comp_target.h"
#include "wivrn_controller.h"
#include "wivrn_hmd.h"

#include "xrt/xrt_session.h"
#include <cmath>
#include <vulkan/vulkan.h>

struct wivrn_comp_target_factory : public comp_target_factory
{
	std::shared_ptr<wivrn_session> session;
	float fps;

	wivrn_comp_target_factory(std::shared_ptr<wivrn_session> session, float fps) :
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
		self->session->comp_target = new wivrn_comp_target(self->session, c, self->fps);
		*out_ct = self->session->comp_target;
		return true;
	}
};

void xrt::drivers::wivrn::max_accumulator::send(wivrn_connection & connection)
{
	if (std::chrono::steady_clock::now() < next_sample)
		return;

	if (auto offset = max.exchange(0))
		connection.send_stream(to_headset::prediction_offset{
		        // Add a 5% margin
		        .offset = std::chrono::nanoseconds(offset * 21 / 20),
		});
	next_sample += std::chrono::seconds(1);
}

xrt::drivers::wivrn::wivrn_session::wivrn_session(xrt::drivers::wivrn::TCP && tcp, u_system & system) :
        connection(std::move(tcp)), xrt_system(system)
{
}

xrt_result_t xrt::drivers::wivrn::wivrn_session::create_session(xrt::drivers::wivrn::TCP && tcp,
                                                                u_system & system,
                                                                xrt_system_devices ** out_xsysd,
                                                                xrt_space_overseer ** out_xspovrs,
                                                                xrt_system_compositor ** out_xsysc)
{
	std::shared_ptr<wivrn_session> self;
	std::optional<xrt::drivers::wivrn::from_headset::packets> control;
	try
	{
		self = std::shared_ptr<wivrn_session>(new wivrn_session(std::move(tcp), system));
		while (not(control = self->connection.poll_control(-1)))
		{
			// FIXME: timeout
		}
	}
	catch (std::exception & e)
	{
		U_LOG_E("Error creating WiVRn session: %s", e.what());
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}

	const auto & info = std::get<from_headset::headset_info_packet>(*control);

	try
	{
		self->audio_handle = audio_device::create(
		        "wivrn.source",
		        "WiVRn(microphone)",
		        "wivrn.sink",
		        "WiVRn",
		        info,
		        *self);
		if (self->audio_handle)
			self->send_control(self->audio_handle->description());
	}
	catch (const std::exception & e)
	{
		U_LOG_E("Failed to register audio device: %s", e.what());
		return XRT_ERROR_DEVICE_CREATION_FAILED;
	}
	self->hmd = std::make_unique<wivrn_hmd>(self, info);
	self->left_hand = std::make_unique<wivrn_controller>(0, self->hmd.get(), self);
	self->right_hand = std::make_unique<wivrn_controller>(1, self->hmd.get(), self);

	auto * usysds = u_system_devices_static_allocate();
	*out_xsysd = &usysds->base.base;
	auto & devices = *out_xsysd;
	int n = 0;
	if (self->hmd)
		usysds->base.base.static_roles.head = devices->xdevs[n++] = self->hmd.get();
	if (self->left_hand)
	{
		devices->xdevs[n++] = self->left_hand.get();
		usysds->base.base.static_roles.hand_tracking.left = self->left_hand.get();
	}
	if (self->right_hand)
	{
		devices->xdevs[n++] = self->right_hand.get();
		usysds->base.base.static_roles.hand_tracking.right = self->right_hand.get();
	}
	devices->xdev_count = n;

	u_system_devices_static_finalize(usysds, self->left_hand.get(), self->right_hand.get());

	wivrn_comp_target_factory ctf(self, info.preferred_refresh_rate);
	auto xret = comp_main_create_system_compositor(self->hmd.get(), &ctf, out_xsysc);
	if (xret != XRT_SUCCESS)
	{
		U_LOG_E("Failed to create system compositor");
		return xret;
	}

	u_builder_create_space_overseer_legacy(
	        &self->xrt_system.broadcast,
	        self->hmd.get(),
	        self->left_hand.get(),
	        self->right_hand.get(),
	        devices->xdevs,
	        devices->xdev_count,
	        false,
	        out_xspovrs);

	devices->destroy = [](xrt_system_devices * xsd) {
		// TODO
	};

	auto dump_file = std::getenv("WIVRN_DUMP_TIMINGS");
	if (dump_file)
	{
		self->feedback_csv.open(dump_file);
	}

	self->thread = std::thread(&wivrn_session::run, self);
	return XRT_SUCCESS;
}

clock_offset wivrn_session::get_offset()
{
	return offset_est.get_offset();
}

bool wivrn_session::connected()
{
	return connection.is_active();
}

void wivrn_session::operator()(from_headset::headset_info_packet &&)
{
	U_LOG_W("unexpected headset info packet, ignoring");
}
void wivrn_session::operator()(from_headset::tracking && tracking)
{
	auto offset = offset_est.get_offset();
	if (not offset)
		return;

	hmd->update_tracking(tracking, offset);
	left_hand->update_tracking(tracking, offset);
	right_hand->update_tracking(tracking, offset);
}

void wivrn_session::operator()(from_headset::hand_tracking && hand_tracking)
{
	auto offset = offset_est.get_offset();
	if (not offset)
		return;

	left_hand->update_hand_tracking(hand_tracking, offset);
	right_hand->update_hand_tracking(hand_tracking, offset);
}

void wivrn_session::operator()(from_headset::inputs && inputs)
{
	auto offset = get_offset();
	if (not offset)
		return;
	left_hand->set_inputs(inputs, offset);
	right_hand->set_inputs(inputs, offset);
}

void wivrn_session::operator()(from_headset::timesync_response && timesync)
{
	offset_est.add_sample(timesync);
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

void wivrn_session::operator()(audio_data && data)
{
	if (audio_handle)
		audio_handle->process_mic_data(std::move(data));
}

void wivrn_session::run(std::weak_ptr<wivrn_session> weak_self)
{
	while (true)
	{
		try
		{
			auto self = weak_self.lock();
			if (self and not self->quit)
			{
				self->offset_est.request_sample(self->connection);
				self->predict_offset.send(self->connection);
				self->connection.poll(*self, 20);
			}
			else
				return;
		}
		catch (const std::exception & e)
		{
			U_LOG_E("Exception in network thread: %s", e.what());
			auto self = weak_self.lock();
			if (self)
				self->reconnect();
		}
	}
}

std::array<to_headset::video_stream_description::foveation_parameter, 2> wivrn_session::set_foveated_size(uint32_t width, uint32_t height)
{
	return hmd->set_foveated_size(width, height);
}

void wivrn_session::dump_time(const std::string & event, uint64_t frame, uint64_t time, uint8_t stream, const char * extra)
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

	try
	{
		offset_est.reset();
		connection.reset(std::move(*tcp));
		std::optional<xrt::drivers::wivrn::from_headset::packets> control;
		while (not(control = connection.poll_control(-1)))
		{
			// FIXME: timeout
		}
		const auto & info = std::get<from_headset::headset_info_packet>(*control);
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
	catch (const std::exception & e)
	{
		U_LOG_E("Reconnection failed: %s", e.what());
	}
}
