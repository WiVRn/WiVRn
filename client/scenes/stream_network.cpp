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

#ifdef __ANDROID__
#include "application.h"
#endif

#include "utils/named_thread.h"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <uni_algo/case.h>

void scenes::stream::process_packets()
{
#ifdef __ANDROID__
	application::instance().setup_jni();
#endif
	while (not exiting)
	{
		try
		{
			network_session->poll(*this, std::chrono::milliseconds(500));
		}
		catch (std::exception & e)
		{
			spdlog::info("Exception in network thread, exiting: {}", e.what());
			exit();
		}
	}
}

void scenes::stream::operator()(to_headset::video_stream_data_shard && shard)
{
	std::shared_lock lock(decoder_mutex);
	uint8_t idx = shard.stream_item_idx;
	if (idx >= decoders.size())
	{
		// We don't know (yet?) about this stream, ignore packet
		return;
	}
	decoders[idx].decoder->push_shard(std::move(shard));
}

void scenes::stream::operator()(to_headset::audio_stream_description && desc)
{
	audio_handle.emplace(desc, *network_session, instance);
}

void scenes::stream::operator()(to_headset::video_stream_description && desc)
{
	setup(desc);

	if (not tracking_thread)
	{
		tracking_thread = utils::named_thread("tracking_thread", &stream::tracking, this);
	}
}

void scenes::stream::operator()(to_headset::refresh_rate_change && rate)
{
	session.set_refresh_rate(rate.fps);
}

void scenes::stream::operator()(to_headset::timesync_query && query)
{
	network_session->send_stream(from_headset::timesync_response{
	        .query = query.query,
	        .response = instance.now(),
	});
}

void scenes::stream::operator()(audio_data && data)
{
	if (audio_handle)
		(*audio_handle)(std::move(data));
}

void scenes::stream::send_feedback(const wivrn::from_headset::feedback & feedback)
{
	try
	{
		network_session->send_control(wivrn::from_headset::feedback{feedback});
	}
	catch (std::exception & e)
	{
		spdlog::warn("Exception while sending feedback packet: {}", e.what());
	}
}

void scenes::stream::operator()(to_headset::application_list && apps)
{
	std::ranges::sort(apps.applications, [](auto & l, auto & r) {
		return una::casesens::collate_utf8(l.name, r.name) < 0;
	});
	auto locked = applications.lock();
	*locked = std::move(apps);
}

void scenes::stream::start_application(std::string appid)
{
	network_session->send_control(wivrn::from_headset::start_app{
	        .app_id = std::move(appid),
	});
}
