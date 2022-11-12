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

#include "stream.h"

void scenes::stream::process_packets()
{
	while (not exiting)
	{
		try
		{
			int n = network_session->poll(*this, std::chrono::milliseconds(5000));
			if (n == 0 && video_started_)
				throw std::runtime_error("Timeout waiting for network packets");
		}
		catch (std::exception & e)
		{
			spdlog::info("Exception in network thread, exiting: {}", e.what());
			exiting = true;
		}
	}
}

void scenes::stream::operator()(to_headset::video_stream_data_shard && shard)
{
	video_started_ = true;
	shard_queue.push(std::move(shard));
}

void scenes::stream::operator()(to_headset::video_stream_parity_shard && shard)
{
	video_started_ = true;
	shard_queue.push(std::move(shard));
}

void scenes::stream::operator()(to_headset::video_stream_description && desc)
{
	setup(desc);

	if (not tracking_thread)
	{
		tracking_thread = std::thread(&stream::tracking, this);
		pthread_setname_np(tracking_thread->native_handle(), "tracking_thread");
	}
}

void scenes::stream::operator()(to_headset::timesync_query && query)
{
	from_headset::timesync_response response{};
	response.query = query.query;
	response.response = instance.now();
	network_session->send_stream(response);
}

void scenes::stream::send_feedback(const xrt::drivers::wivrn::from_headset::feedback & feedback)
{
	try
	{
		network_session->send_control(feedback);
	}
	catch (std::exception & e)
	{
		spdlog::warn("Exception while sending feedback packet: {}", e.what());
	}
}
