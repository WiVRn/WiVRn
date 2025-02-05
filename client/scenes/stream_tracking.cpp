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

#include "application.h"
#include "stream.h"
#include <ranges>
#include <spdlog/spdlog.h>
#include <thread>

#ifdef __ANDROID__
#include "android/battery.h"
#include "android/jnipp.h"
#endif

using tid = to_headset::tracking_control::id;

static const XrDuration min_tracking_period = 2'000'000;
static const XrDuration max_tracking_period = 5'000'000;

static from_headset::tracking::pose locate_space(device_id device, XrSpace space, XrSpace reference, XrTime time)
{
	XrSpaceVelocity velocity{
	        .type = XR_TYPE_SPACE_VELOCITY,
	};

	XrSpaceLocation location{
	        .type = XR_TYPE_SPACE_LOCATION,
	        .next = &velocity,
	};

	xrLocateSpace(space, reference, time, &location);

	from_headset::tracking::pose res{
	        .pose = location.pose,
	        .linear_velocity = velocity.linearVelocity,
	        .angular_velocity = velocity.angularVelocity,
	        .device = device,
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

namespace
{
class timer
{
	xr::instance & instance;
	XrTime start = instance.now();

public:
	timer(xr::instance & instance) :
	        instance(instance) {}
	XrDuration count()
	{
		return instance.now() - start;
	}
};
} // namespace

static std::optional<std::array<from_headset::hand_tracking::pose, XR_HAND_JOINT_COUNT_EXT>> locate_hands(xr::hand_tracker & hand, XrSpace space, XrTime time)
{
	auto joints = hand.locate(space, time);

	if (joints)
	{
		std::array<from_headset::hand_tracking::pose, XR_HAND_JOINT_COUNT_EXT> poses;
		for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
		{
			poses[i] = {
			        .pose = (*joints)[i].first.pose,
			        .linear_velocity = (*joints)[i].second.linearVelocity,
			        .angular_velocity = (*joints)[i].second.angularVelocity,
			        .radius = uint16_t((*joints)[i].first.radius * 10'000),
			};

			if ((*joints)[i].first.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
				poses[i].flags |= from_headset::hand_tracking::orientation_valid;

			if ((*joints)[i].first.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
				poses[i].flags |= from_headset::hand_tracking::position_valid;

			if ((*joints)[i].second.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
				poses[i].flags |= from_headset::hand_tracking::linear_velocity_valid;

			if ((*joints)[i].second.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
				poses[i].flags |= from_headset::hand_tracking::angular_velocity_valid;

			if ((*joints)[i].first.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
				poses[i].flags |= from_headset::hand_tracking::orientation_tracked;

			if ((*joints)[i].first.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
				poses[i].flags |= from_headset::hand_tracking::position_tracked;
		}

		return poses;
	}
	else
		return std::nullopt;
}

static bool enabled(const to_headset::tracking_control & control, device_id id)
{
	switch (id)
	{
		case device_id::HEAD:
		case device_id::EYE_GAZE:
			return true;
		case device_id::LEFT_AIM:
			return control.enabled[size_t(tid::left_aim)];
		case device_id::LEFT_GRIP:
			return control.enabled[size_t(tid::left_grip)];
		case device_id::LEFT_PALM:
			return control.enabled[size_t(tid::left_palm)];
		case device_id::RIGHT_AIM:
			return control.enabled[size_t(tid::right_aim)];
		case device_id::RIGHT_GRIP:
			return control.enabled[size_t(tid::right_grip)];
		case device_id::RIGHT_PALM:
			return control.enabled[size_t(tid::right_palm)];
		default:
			break;
	}
	__builtin_unreachable();
}

template <typename T>
static T & from_pool(std::vector<T> & container, std::vector<T> & pool)
{
	if (pool.empty())
		return container.emplace_back();
	auto & result = container.emplace_back(std::move(pool.back()));
	pool.pop_back();
	return result;
}

void scenes::stream::tracking()
{
#ifdef __ANDROID__
	// Runtime may use JNI and needs the thread to be attached
	application::instance().setup_jni();

	XrTime next_battery_check = 0;
	const XrDuration battery_check_interval = 30'000'000'000; // 30s
#endif
	std::vector<std::pair<device_id, XrSpace>> spaces = {
	        {device_id::HEAD, application::space(xr::spaces::view)},
	        {device_id::LEFT_AIM, application::space(xr::spaces::aim_left)},
	        {device_id::LEFT_GRIP, application::space(xr::spaces::grip_left)},
	        {device_id::RIGHT_AIM, application::space(xr::spaces::aim_right)},
	        {device_id::RIGHT_GRIP, application::space(xr::spaces::grip_right)}};

	if (XrSpace palm = application::space(xr::spaces::palm_left))
		spaces.emplace_back(device_id::LEFT_PALM, palm);
	if (XrSpace palm = application::space(xr::spaces::palm_right))
		spaces.emplace_back(device_id::RIGHT_PALM, palm);

	const auto & config = application::get_config();

	if (config.check_feature(feature::eye_gaze))
		spaces.push_back({device_id::EYE_GAZE, application::space(xr::spaces::eye_gaze)});

	XrSpace view_space = application::space(xr::spaces::view);
	XrSpace world_space = application::space(xr::spaces::world);
	XrDuration tracking_period = min_tracking_period;     // target period for 20% busy time
	XrDuration current_tracking_period = tracking_period; // divider of frame period
	int period_adjust = 0;

	XrTime t0 = instance.now();
	XrTime last_hand_sample = t0;
	std::vector<from_headset::tracking> tracking;
	std::vector<from_headset::tracking> tracking_pool; // pre-allocated objects
	std::vector<from_headset::hand_tracking> hands;

	std::vector<from_headset::trackings> merged_tracking;
	std::vector<serialization_packet> packets;

	const bool hand_tracking = config.check_feature(feature::hand_tracking);
	const bool face_tracking = config.check_feature(feature::face_tracking);

	on_interaction_profile_changed({});

	while (not exiting)
	{
		try
		{
			tracking.clear();
			hands.clear();

			XrTime now = instance.now();
			if (now < t0)
				std::this_thread::sleep_for(std::chrono::nanoseconds(t0 - now));

			// If thread can't keep up, skip timestamps
			t0 = std::max(t0, now);

			timer t(instance);
			int samples = 0;

			to_headset::tracking_control control;
			{
				std::lock_guard lock(tracking_control_mutex);
				control = tracking_control;
			}

			XrDuration prediction = std::clamp<XrDuration>(control.max_offset.count(), 0, 80'000'000);
			auto period = std::max<XrDuration>(display_time_period.load(), 1'000'000);
			for (XrDuration Δt = display_time_phase - t0 % period + (control.min_offset.count() / period) * period;
			     Δt <= prediction + period / 2;
			     Δt += period, ++samples)
			{
				auto & packet = from_pool(tracking, tracking_pool);
				packet.production_timestamp = t0;
				packet.timestamp = t0 + Δt;

				try
				{
					auto [flags, views] = session.locate_views(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, t0 + Δt, view_space);
					assert(views.size() == packet.views.size());

					for (auto [i, j]: std::views::zip(views, packet.views))
					{
						j.pose = i.pose;
						j.fov = i.fov;
					}

					packet.view_flags = flags;

					packet.state_flags = 0;
					if (recenter_requested.exchange(false))
						packet.state_flags = wivrn::from_headset::tracking::recentered;

					packet.device_poses.clear();
					for (auto [device, space]: spaces)
					{
						if (enabled(control, device))
							packet.device_poses.emplace_back(locate_space(device, space, world_space, t0 + Δt));
					}

					// Hand tracking data are very large, send fewer samples than other items
					if (hand_tracking and t0 >= last_hand_sample + period and
					    (Δt == 0 or Δt >= prediction - 2 * period))
					{
						last_hand_sample = t0;
						if (control.enabled[size_t(tid::left_hand)])
							hands.emplace_back(
							        t0,
							        t0 + Δt,
							        from_headset::hand_tracking::left,
							        locate_hands(application::get_left_hand(), world_space, t0 + Δt));

						if (control.enabled[size_t(tid::right_hand)])
							hands.emplace_back(
							        t0,
							        t0 + Δt,
							        from_headset::hand_tracking::right,
							        locate_hands(application::get_right_hand(), world_space, t0 + Δt));
					}

					if (face_tracking and control.enabled[size_t(tid::face)])
					{
						application::get_fb_face_tracker2().get_weights(t0 + Δt, packet.face.emplace());
					}
				}
				catch (const std::system_error & e)
				{
					if (e.code().category() != xr::error_category() or
					    e.code().value() != XR_ERROR_TIME_INVALID)
						throw;
				}
			}

			XrDuration busy_time = t.count();
			// Target: polling between 1 and 5ms, with 20% busy time
			tracking_period = std::clamp<XrDuration>(std::lerp(tracking_period, busy_time * 5, 0.1), min_tracking_period, max_tracking_period);

#ifdef __ANDROID__
			if (next_battery_check < now and control.enabled[size_t(tid::battery)])
			{
				timer t2(instance);

				auto status = get_battery_status();
				network_session->send_stream(from_headset::battery{
				        .charge = status.charge.value_or(-1),
				        .present = status.charge.has_value(),
				        .charging = status.charging,
				});

				next_battery_check = now + battery_check_interval;
				XrDuration battery_dur = t2.count();

				spdlog::info("Battery check took: {}", battery_dur);
			}
#endif

			merged_tracking.clear();
			size_t current_size = 1400;
			for (auto & item: tracking)
			{
				size_t size = serialized_size(item);
				if (size + current_size > 1400)
				{
					merged_tracking.emplace_back();
					current_size = 0;
				}
				current_size += size;
				merged_tracking.back().items.emplace_back(std::move(item));
			}

			packets.resize(std::max(packets.size(), merged_tracking.size() + hands.size()));
			size_t packet_count = 0;
			for (const auto & i: merged_tracking)
			{
				auto & packet = packets[packet_count++];
				packet.clear();
				wivrn_session::stream_socket_t::serialize(packet, i);
			}
			for (const auto & i: hands)
			{
				if (i.joints)
				{
					auto & packet = packets[packet_count++];
					packet.clear();
					wivrn_session::stream_socket_t::serialize(packet, i);
				}
			}

			network_session->send_stream(std::span(packets.data(), packet_count));

			for (auto & item: merged_tracking)
				std::ranges::move(item.items, std::back_inserter(tracking_pool));

			if (period_adjust == 0)
			{
				if (auto p = real_display_period.load())
				{
					period_adjust = (p / tracking_period);
					current_tracking_period = p / period_adjust;
				}
			}
			else
				--period_adjust;

			t0 += current_tracking_period;
		}
		catch (std::exception & e)
		{
			spdlog::info("Exception in tracking thread, exiting: {}", e.what());
			exit();
		}
	}
}

void scenes::stream::operator()(to_headset::tracking_control && packet)
{
	std::lock_guard lock(tracking_control_mutex);
	auto m = size_t(to_headset::tracking_control::id::microphone);
	if (audio_handle)
		audio_handle->set_mic_sate(packet.enabled[m]);

	tracking_control = packet;
	tracking_control.min_offset = std::min(tracking_control.min_offset, tracking_control.max_offset);
}

void scenes::stream::on_interaction_profile_changed(const XrEventDataInteractionProfileChanged & event)
{
	auto now = instance.now();
	for (auto [target, space]: {
	             std::tuple{device_id::LEFT_AIM, application::space(xr::spaces::aim_left)},
	             {device_id::LEFT_PALM, application::space(xr::spaces::palm_left)},
	             {device_id::RIGHT_AIM, application::space(xr::spaces::aim_right)},
	             {device_id::RIGHT_PALM, application::space(xr::spaces::palm_right)},
	     })
	{
		device_id source;
		XrSpace source_space = XR_NULL_HANDLE;
		switch (target)
		{
			case device_id::LEFT_AIM:
			case device_id::LEFT_PALM:
				source = device_id::LEFT_GRIP;
				source_space = application::space(xr::spaces::grip_left);
				break;
			case device_id::RIGHT_AIM:
			case device_id::RIGHT_PALM:
				source = device_id::RIGHT_GRIP;
				source_space = application::space(xr::spaces::grip_right);
				break;
			default:
				continue;
		}
		auto pose = locate_space(target, space, source_space, now);

		if (pose.flags & from_headset::tracking::position_valid and pose.flags & from_headset::tracking::orientation_valid)
		{
			network_session->send_control(from_headset::derived_pose{
			        .source = source,
			        .target = target,
			        .relation = pose.pose,
			});
		}
		else
		{
			network_session->send_control(from_headset::derived_pose{
			        .source = target,
			        .target = target,
			});
		}
	}
}
