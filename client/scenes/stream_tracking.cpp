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
#include "utils/ranges.h"
#include <spdlog/spdlog.h>
#include <thread>

#ifdef __ANDROID__
#include "jnipp.h"
#endif

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

namespace
{
class timer
{
	xr::instance & instance;
	XrTime start = instance.now();
	XrDuration duration = 0;

public:
	timer(xr::instance & instance) :
	        instance(instance) {}
	void pause()
	{
		duration += instance.now() - start;
	}
	void resume()
	{
		start = instance.now();
	}
	XrDuration count()
	{
		pause();
		return duration;
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
	const XrDuration battery_check_interval = 5'000'000'000; // 5s
#endif
	std::vector<std::pair<device_id, XrSpace>> spaces = {
	        {device_id::HEAD, application::view()},
	        {device_id::LEFT_AIM, application::left_aim()},
	        {device_id::LEFT_GRIP, application::left_grip()},
	        {device_id::RIGHT_AIM, application::right_aim()},
	        {device_id::RIGHT_GRIP, application::right_grip()}};

	if (application::get_eye_gaze_supported())
		spaces.push_back({device_id::EYE_GAZE, application::eye_gaze()});

	XrSpace view_space = application::view();
	XrDuration tracking_period = 1'000'000; // Send tracking data every 1ms
	const XrDuration dt = 100'000;          // Wake up 0.1ms before measuring the position

	XrTime t0 = instance.now();
	from_headset::tracking packet{};

	while (not exiting)
	{
		try
		{
			XrTime now = instance.now();
			if (now < t0)
				std::this_thread::sleep_for(std::chrono::nanoseconds(t0 - now - dt));

			// If thread can't keep up, skip timestamps
			t0 = std::max(t0, now);

			timer t(instance);

			XrDuration prediction = std::min<XrDuration>(tracking_prediction_offset, 80'000'000);
			auto period = std::max<XrDuration>(display_time_period.load(), 1'000'000);
			// 1 or 2 samples
			for (XrDuration Δt = 0; Δt <= prediction + period / 2; Δt += period)
			{
				from_headset::hand_tracking hands{};
				from_headset::fb_face2 fb_face2{};

				packet.production_timestamp = t0;
				hands.production_timestamp = t0;
				fb_face2.production_timestamp = t0;

				packet.timestamp = t0 + Δt;
				hands.timestamp = t0 + Δt;
				fb_face2.timestamp = t0 + Δt;

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

					packet.device_poses.clear();
					std::lock_guard lock(local_floor_mutex);
					for (auto [device, space]: spaces)
					{
						packet.device_poses.push_back(locate_space(device, space, local_floor, t0 + Δt));
					}

					t.pause();
					network_session->send_stream(packet);
					t.resume();

					if (application::get_hand_tracking_supported())
					{
						hands.hand = xrt::drivers::wivrn::from_headset::hand_tracking::left;
						hands.joints = locate_hands(application::get_left_hand(), local_floor, hands.timestamp);
						t.pause();
						network_session->send_stream(hands);
						t.resume();

						hands.hand = xrt::drivers::wivrn::from_headset::hand_tracking::right;
						hands.joints = locate_hands(application::get_right_hand(), local_floor, hands.timestamp);
						t.pause();
						network_session->send_stream(hands);
						t.resume();
					}

					if (application::get_fb_face_tracking2_supported())
					{
						application::get_fb_face_tracker2().get_weights(t0 + Δt, &fb_face2);
						t.pause();
						network_session->send_stream(fb_face2);
						t.resume();
					}

					XrDuration busy_time = t.count();
					// Target: polling between 1 and 5ms, with 20% busy time
					tracking_period = std::clamp<XrDuration>(std::lerp(tracking_period, busy_time * 5, 0.2), 1'000'000, 5'000'000);
				}
				catch (const std::system_error & e)
				{
					if (e.code().category() != xr::error_category() or
					    e.code().value() != XR_ERROR_TIME_INVALID)
						throw;
				}

				// Make sure predictions are phase-synced with display time
				if (Δt == 0 and prediction)
					Δt = display_time_phase - t0 % period;
			}

#ifdef __ANDROID__
			if (next_battery_check < now)
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

			t0 += tracking_period;
		}
		catch (std::exception & e)
		{
			spdlog::info("Exception in tracking thread, exiting: {}", e.what());
			exit();
		}
	}
}

void scenes::stream::operator()(to_headset::prediction_offset && packet)
{
	if (packet.offset.count() >= 0)
		tracking_prediction_offset = std::lerp(packet.offset.count(), tracking_prediction_offset.load(), 0.2);
}
