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
#include "os/os_time.h"
#include "util/u_logging.h"

#include "wivrn_controller.h"
#include "wivrn_hmd.h"

xrt::drivers::wivrn::wivrn_session::wivrn_session(xrt::drivers::wivrn::TCP && tcp, in6_addr & address) :
        connection(std::move(tcp), address)
{
}

xrt_system_devices * xrt::drivers::wivrn::wivrn_session::create_session(xrt::drivers::wivrn::TCP && tcp)
{
	sockaddr_in6 address;
	socklen_t address_len = sizeof(address);
	if (getpeername(tcp.get_fd(), (sockaddr *)&address, &address_len) < 0)
	{
		U_LOG_E("Cannot get peer address: %s", strerror(errno));
		return nullptr;
	}

	std::shared_ptr<wivrn_session> self;
	std::optional<xrt::drivers::wivrn::from_headset::control_packets> control;
	try
	{
		self = std::shared_ptr<wivrn_session>(new wivrn_session(std::move(tcp), address.sin6_addr));
		while (not (control = self->connection.poll_control(-1))) {}
	}
	catch (std::exception & e)
	{
		U_LOG_E("Error creating WiVRn session: %s", e.what());
		return nullptr;
	}

	const auto & info = std::get<from_headset::headset_info_packet>(*control);
	self->hmd = std::make_unique<wivrn_hmd>(self, info);
	self->left_hand = std::make_unique<wivrn_controller>(0, self->hmd.get(), self);
	self->right_hand = std::make_unique<wivrn_controller>(1, self->hmd.get(), self);

	xrt_system_devices * devices = new xrt_system_devices{};

	int n = 0;
	if (self->hmd)
		devices->roles.head = devices->xdevs[n++] = self->hmd.get();
	if (self->left_hand)
		devices->roles.left = devices->xdevs[n++] = self->left_hand.get();
	if (self->right_hand)
		devices->roles.right = devices->xdevs[n++] = self->right_hand.get();

	devices->xdev_count = n;
	devices->destroy = [](xrt_system_devices * xsd) {
		// TODO
	};

	auto dump_file = std::getenv("WIVRN_DUMP_TIMINGS");
	if (dump_file)
	{
		self->feedback_csv.open(dump_file);
	}

	self->thread = std::thread(&wivrn_session::run, self);
	return devices;
}

clock_offset wivrn_session::get_offset()
{
	std::lock_guard lock(mutex);
	return offset;
}

void wivrn_session::operator()(from_headset::headset_info_packet &&)
{
	U_LOG_W("unexpected headset info packet, ignoring");
}
void wivrn_session::operator()(from_headset::tracking && tracking)
{
	if (offset.epoch_offset.count() == 0)
		return;

	hmd->update_tracking(tracking, offset);
	left_hand->update_tracking(tracking, offset);
	right_hand->update_tracking(tracking, offset);
}

void wivrn_session::operator()(from_headset::inputs && inputs)
{
	left_hand->set_inputs(inputs);
	right_hand->set_inputs(inputs);
}

void wivrn_session::operator()(from_headset::timesync_response && timesync)
{
	std::lock_guard lock(mutex);
	offset.epoch_offset = std::chrono::nanoseconds(timesync.response) - timesync.query;
	offset_age = std::chrono::steady_clock::now();
}

void wivrn_session::operator()(from_headset::feedback && feedback)
{

	clock_offset o;
	{
		std::lock_guard lock(mutex);
		o = offset;
	}

	if (feedback.received_first_packet)
		dump_time("receive_start", feedback.frame_index, o.from_headset(feedback.received_first_packet), feedback.stream_index);
	if (feedback.received_last_packet)
		dump_time("receive_end", feedback.frame_index, o.from_headset(feedback.received_last_packet), feedback.stream_index);
	if (feedback.reconstructed)
		dump_time("reconstructed", feedback.frame_index, o.from_headset(feedback.reconstructed), feedback.stream_index);
	if (feedback.sent_to_decoder)
		dump_time("decode_start", feedback.frame_index, o.from_headset(feedback.sent_to_decoder), feedback.stream_index);
	if (feedback.received_from_decoder)
		dump_time("decode_end", feedback.frame_index, o.from_headset(feedback.received_from_decoder), feedback.stream_index);
	if (feedback.blitted)
		dump_time("blit", feedback.frame_index, o.from_headset(feedback.blitted), feedback.stream_index);
	if (feedback.displayed)
		dump_time("display", feedback.frame_index, o.from_headset(feedback.displayed), feedback.stream_index);
}

uint64_t clock_offset::from_headset(uint64_t ts) const
{
	return ts - epoch_offset.count();
}

std::chrono::nanoseconds clock_offset::to_headset(uint64_t timestamp_ns) const
{
	return std::chrono::nanoseconds(timestamp_ns) + epoch_offset;
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
				{
					std::lock_guard lock(self->mutex);
					if (std::chrono::steady_clock::now() - self->offset_age >
					    std::chrono::seconds(5))
					{
						to_headset::timesync_query timesync{};
						timesync.query = std::chrono::nanoseconds(os_monotonic_get_ns());
						self->connection.send_stream(timesync);
					}
				}
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
			{
				self->quit = true;
				exit(0);
			}
			return;
		}
	}
}

void wivrn_session::dump_time(const std::string &event, uint64_t frame, uint64_t time, uint8_t stream)
{
	if (feedback_csv)
	{
		std::lock_guard lock(csv_mutex);
		feedback_csv << std::quoted(event) << "," << frame << "," << time << "," << (int)stream << std::endl;
	}
}
