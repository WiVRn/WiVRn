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
#include "android/jnipp.h"
#endif

using tid = to_headset::tracking_control::id;

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

void scenes::stream::tracking()
{
#ifdef __ANDROID__
	// Runtime may use JNI and needs the thread to be attached
	application::instance().setup_jni();
	jni::object<""> act(application::native_app()->activity->clazz);
	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

	jni::string filter_jstr("android.intent.action.BATTERY_CHANGED");
	jni::string level_jstr("level");
	jni::string scale_jstr("scale");
	jni::string plugged_jstr("plugged");
	jni::Int default_jint(-1);

	auto receiver_jobj = jni::object<"android/content/BroadcastReceiver">(NULL);
	auto filter_jobj = jni::new_object<"android/content/IntentFilter">(filter_jstr);

	auto register_receiver = jni::klass("android/content/Context")
	                                 .method<jni::object<"android/content/Intent">>("registerReceiver", receiver_jobj, filter_jobj);
	auto get_int_extra = jni::klass("android/content/Intent").method<jni::Int>("getIntExtra", level_jstr, default_jint);

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

	if (application::get_eye_gaze_supported())
		spaces.push_back({device_id::EYE_GAZE, application::space(xr::spaces::eye_gaze)});

	XrSpace view_space = application::space(xr::spaces::view);
	XrSpace world_space = application::space(xr::spaces::world);
	XrDuration tracking_period = 1'000'000; // Send tracking data every 1ms
	const XrDuration dt = 100'000;          // Wake up 0.1ms before measuring the position

	XrTime t0 = instance.now();
	std::vector<from_headset::tracking> tracking;
	std::vector<from_headset::hand_tracking> hands;
	int skip_samples = 0;

	while (not exiting)
	{
		try
		{
			tracking.clear();
			hands.clear();

			XrTime now = instance.now();
			if (now < t0)
				std::this_thread::sleep_for(std::chrono::nanoseconds(t0 - now - dt));

			// If thread can't keep up, skip timestamps
			t0 = std::max(t0, now);

			timer t(instance);
			int samples = 0;

			to_headset::tracking_control control;
			{
				std::lock_guard lock(tracking_control_mutex);
				control = tracking_control;
			}

			XrDuration prediction = std::clamp<XrDuration>(control.offset.count(), 0, 80'000'000);
			auto period = std::max<XrDuration>(display_time_period.load(), 1'000'000);
			for (XrDuration Δt = 0; Δt <= prediction + period / 2; Δt += period, ++samples)
			{
				auto & packet = tracking.emplace_back();
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
							packet.device_poses.push_back(locate_space(device, space, world_space, t0 + Δt));
					}

					if (application::get_hand_tracking_supported())
					{
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

					if (application::get_fb_face_tracking2_supported() and control.enabled[size_t(tid::face)])
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

				// Make sure predictions are phase-synced with display time
				if (Δt == 0 and prediction)
				{
					Δt = display_time_phase - t0 % period + skip_samples * period;
				}
			}

			XrDuration busy_time = t.count();
			// Target: polling between 1 and 5ms, with 20% busy time
			tracking_period = std::clamp<XrDuration>(std::lerp(tracking_period, busy_time * 5, 0.2), 1'000'000, 5'000'000);

			if (samples and busy_time / samples > 2'000'000)
			{
				skip_samples = busy_time / 2'000'000;
			}

#ifdef __ANDROID__
			if (next_battery_check < now and control.enabled[size_t(tid::battery)])
			{
				timer t2(instance);
				from_headset::battery battery{};
				auto intent = ctx.call<jni::object<"android/content/Intent">>(register_receiver, receiver_jobj, filter_jobj);
				if (intent)
				{
					auto level_jint = intent.call<jni::Int>(get_int_extra, level_jstr, default_jint);
					auto scale_jint = intent.call<jni::Int>(get_int_extra, scale_jstr, default_jint);

					if (level_jint && level_jint.value >= 0 && scale_jint && scale_jint.value >= 0)
					{
						battery.present = true;
						battery.charge = (float)(level_jint.value) / (float)(scale_jint.value);
					}

					auto plugged_jint = intent.call<jni::Int>(get_int_extra, plugged_jstr, default_jint);
					battery.charging = plugged_jint && plugged_jint.value > 0;
				}

				network_session->send_stream(battery);

				next_battery_check = now + battery_check_interval;
				XrDuration battery_dur = t2.count();

				spdlog::info("Battery check took: {}", battery_dur);
			}
#endif

			std::vector<from_headset::trackings> merged_tracking(1);
			size_t current_size = 0;
			for (auto & item: tracking)
			{
				size_t size = serialized_size(item);
				if (size + current_size > 1400)
				{
					merged_tracking.emplace_back();
					current_size = 0;
				}
				current_size += size;
				merged_tracking.back().items.push_back(std::move(item));
			}

			std::vector<serialization_packet> packets;
			packets.reserve(merged_tracking.size() + hands.size());
			for (const auto & i: merged_tracking)
				wivrn_session::stream_socket_t::serialize(packets.emplace_back(), i);
			for (const auto & i: hands)
			{
				if (i.joints)
					wivrn_session::stream_socket_t::serialize(packets.emplace_back(), i);
			}

			network_session->send_stream(std::span(packets));

			t0 += tracking_period;
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
	tracking_control = packet;
}
