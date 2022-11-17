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
#include "utils/ranges.h"
#include <thread>

static from_headset::tracking::pose locate_space(device_id device, XrSpace space, XrSpace reference, XrTime time)
{
	XrSpaceVelocity velocity{
	        .type = XR_TYPE_SPACE_VELOCITY};

	XrSpaceLocation location{
	        .type = XR_TYPE_SPACE_LOCATION,
	        .next = &velocity};

	xrLocateSpace(space, reference, time, &location);

	from_headset::tracking::pose res{
	        .device = device,
	        .pose = location.pose,
	        .linear_velocity = velocity.linearVelocity,
	        .angular_velocity = velocity.angularVelocity,
	        .flags = 0,
	};

	if (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
		res.flags |= from_headset::tracking::orientation_valid;

	if (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
		res.flags |= from_headset::tracking::position_valid;

	if (velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
		res.flags |= from_headset::tracking::linear_velocity_valid;

	if (velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
		res.flags |= from_headset::tracking::angular_velocity_valid;

	if (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
		res.flags |= from_headset::tracking::orientation_tracked;

	if (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
		res.flags |= from_headset::tracking::position_tracked;

	return res;
}

void scenes::stream::tracking()
{
	std::vector<std::pair<device_id, XrSpace>> spaces = {
	        {device_id::HEAD, application::view()},
	        {device_id::LEFT_AIM, application::left_aim()},
	        {device_id::LEFT_GRIP, application::left_grip()},
	        {device_id::RIGHT_AIM, application::right_aim()},
	        {device_id::RIGHT_GRIP, application::right_grip()}};

	XrSpace view_space = application::view();
	const XrDuration tracking_period = 10'000'000; // Send tracking data every 10ms
	const XrDuration dt = 1'000'000;               // Wake up 1ms before measuring the position
	const XrDuration extrapolation_horizon = 1;    // 50'000'000;

	XrTime t0 = instance.now();
	t0 = t0 - t0 % tracking_period;

	while (not exiting)
	{
		try
		{
			XrTime now = instance.now();
			if (now < t0)
				std::this_thread::sleep_for(std::chrono::nanoseconds(t0 - now - dt));

			for (XrTime Δt = 0; Δt < extrapolation_horizon; Δt += tracking_period)
			{
				from_headset::tracking packet{};
				packet.timestamp = t0 + Δt;

				try
				{
					auto [flags, views] = session.locate_views(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, t0 + Δt, view_space);
					assert(views.size() == packet.views.size());

					for (auto [i, j]: utils::zip(views, packet.views))
					{
						j.pose = i.pose;
						j.fov = i.fov;
					}

					packet.flags = flags;

					for (auto [device, space]: spaces)
					{
						packet.device_poses.push_back(locate_space(device, space, world_space, t0 + Δt));
					}

					network_session->send_stream(packet);
				}
				catch (const std::system_error & e)
				{
					if (e.code().category() != xr::error_category() or
					    e.code().value() != XR_ERROR_TIME_INVALID)
						throw;
				}
			}

			t0 += tracking_period;
		}
		catch (std::exception & e)
		{
			spdlog::info("Exception in tracking thread, exiting: {}", e.what());
			exiting = true;
		}
	}
}
