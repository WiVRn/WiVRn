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
#include "utils/overloaded.h"
#include "wivrn_packets.h"
#include "xr/fb_body_tracker.h"
#include <ranges>
#include <spdlog/spdlog.h>
#include <thread>

#ifdef __ANDROID__
#include "android/battery.h"
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

static xr::spaces device_to_space(device_id id)
{
	switch (id)
	{
		case device_id::HEAD:
			return xr::spaces::view;
		case device_id::EYE_GAZE:
			return xr::spaces::eye_gaze;
		case device_id::LEFT_AIM:
			return xr::spaces::aim_left;
		case device_id::LEFT_GRIP:
			return xr::spaces::grip_left;
		case device_id::LEFT_PALM:
			return xr::spaces::palm_left;
		case device_id::RIGHT_AIM:
			return xr::spaces::aim_right;
		case device_id::RIGHT_GRIP:
			return xr::spaces::grip_right;
		case device_id::RIGHT_PALM:
			return xr::spaces::palm_right;
		default:
			assert(false);
			__builtin_unreachable();
	}
}

void scenes::stream::tracking()
{
#ifdef __ANDROID__
	// Runtime may use JNI and needs the thread to be attached
	application::instance().setup_jni();

	XrTime next_battery_check = 0;
	const XrDuration battery_check_interval = 30'000'000'000; // 30s
#endif

	std::vector<std::pair<device_id, XrSpace>> spaces;
	for (auto id: {
	             device_id::HEAD,
	             device_id::LEFT_AIM,
	             device_id::LEFT_GRIP,
	             device_id::LEFT_PALM,
	             device_id::RIGHT_AIM,
	             device_id::RIGHT_GRIP,
	             device_id::RIGHT_PALM,
	     })
	{
		if (XrSpace space = application::space(device_to_space(id)))
			spaces.emplace_back(id, space);
	}

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
	XrTime last_body_sample = t0;
	std::vector<from_headset::tracking> tracking;
	std::vector<from_headset::tracking> tracking_pool; // pre-allocated objects
	std::vector<from_headset::hand_tracking> hands;
	std::array<bool, 2> should_skip_simple_controllers{};
	std::vector<from_headset::body_tracking> body;
	std::vector<XrView> views;

	std::vector<from_headset::trackings> merged_tracking;
	std::vector<serialization_packet> packets;

	const bool hand_tracking = config.check_feature(feature::hand_tracking);
	// Quest hand tracking creates a fake khr/simple_controller when hand tracking
	// is enabled, let's not send it when hand tracking data is invalid
	bool block_khr_simple_controller = false;
	switch (guess_model())
	{
		case model::meta_quest_3:
		case model::meta_quest_pro:
		case model::meta_quest_3s:
		case model::oculus_quest_2:
			block_khr_simple_controller = hand_tracking;
			break;
		default:
			break;
	}

	from_headset::face_type face_tracking = from_headset::face_type::none;
	if (config.check_feature(feature::face_tracking))
	{
		if (application::get_fb_face_tracking2_supported())
			face_tracking = from_headset::face_type::fb2;
		else if (application::get_htc_face_tracking_eye_supported() or application::get_htc_face_tracking_lip_supported())
			face_tracking = from_headset::face_type::htc;
		else if (application::get_pico_face_tracking_supported())
		{
			face_tracking = from_headset::face_type::pico;
			// We can't start the face tracking on application initialisation like we do for
			// other face trackers due to a Pico runtime bug where face tracking freezes when
			// the headset is taken off or the application is quit.
			application::get_pico_face_tracker().start();
		}
	}

	const bool body_tracking = config.check_feature(feature::body_tracking);
	auto & body_tracker = application::get_body_tracker();
	if (body_tracking)
	{
		if (auto * body_fb = std::get_if<xr::fb_body_tracker>(&body_tracker))
		{
			// TODO maybe handle reconnection better, if the settings are changed since
			// last connection to running server, the tracker count will mismatch and
			// stuff might break
			body_fb->stop();
			body_fb->start(config.fb_lower_body, config.fb_hip);
		}
	}

	on_interaction_profile_changed({});

	while (not exiting)
	{
		try
		{
			tracking.clear();
			hands.clear();
			body.clear();

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
					packet.view_flags = session.locate_views(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, t0 + Δt, view_space, views);
					assert(views.size() == packet.views.size());

					for (auto [i, j]: std::views::zip(views, packet.views))
					{
						j.pose = i.pose;
						j.fov = i.fov;
					}

					packet.state_flags = 0;
					if (recenter_requested.exchange(false))
						packet.state_flags = wivrn::from_headset::tracking::recentered;

					// Hand tracking data are very large, send fewer samples than other items
					if (hand_tracking and t0 >= last_hand_sample + period and
					    (Δt == 0 or Δt >= prediction - 2 * period))
					{
						last_hand_sample = t0;
						if (control.enabled[size_t(tid::left_hand)])
						{
							auto joints = locate_hands(application::get_left_hand(), world_space, t0 + Δt);
							should_skip_simple_controllers[0] = !joints.has_value();
							hands.emplace_back(
							        t0,
							        t0 + Δt,
							        from_headset::hand_tracking::left,
							        joints);
						}
						else
						{
							should_skip_simple_controllers[0] = false;
						}

						if (control.enabled[size_t(tid::right_hand)])
						{
							auto joints = locate_hands(application::get_right_hand(), world_space, t0 + Δt);
							should_skip_simple_controllers[1] = !joints.has_value();
							hands.emplace_back(
							        t0,
							        t0 + Δt,
							        from_headset::hand_tracking::right,
							        joints);
						}
						else
						{
							should_skip_simple_controllers[1] = false;
						}
					}

					packet.device_poses.clear();
					for (auto [device, space]: spaces)
					{
						if (enabled(control, device))
						{
							switch (device)
							{
								case device_id::LEFT_AIM:
								case device_id::LEFT_GRIP:
								case device_id::LEFT_PALM:
								case device_id::RIGHT_AIM:
								case device_id::RIGHT_GRIP:
								case device_id::RIGHT_PALM: {
									const bool right = device > device_id::LEFT_PALM;
									if (block_khr_simple_controller and interaction_profiles[right].load() == interaction_profile::khr_simple_controller and should_skip_simple_controllers[right])
									{
										packet.device_poses.emplace_back(from_headset::tracking::pose{
										        .device = device,
										});
										continue;
									}
									[[fallthrough]];
								}
								default:
									packet.device_poses.emplace_back(locate_space(device, space, world_space, t0 + Δt));
									break;
							}
						}
					}

					if (body_tracking)
						std::visit(utils::overloaded{
						                   [](std::monostate &) {},
						                   [&](auto & b) {
							                   if (t0 >= last_body_sample + period and
							                       (Δt == 0 or Δt >= prediction - 2 * period))
							                   {
								                   last_body_sample = t0;
								                   if (control.enabled[size_t(tid::generic_tracker)])
								                   {
									                   body.push_back(from_headset::body_tracking{
									                           .production_timestamp = t0,
									                           .timestamp = t0 + Δt,
									                           .poses = b.locate_spaces(t0 + Δt, world_space),
									                   });
								                   }
							                   }
						                   },
						           },
						           body_tracker);

					if (control.enabled[size_t(tid::face)])
					{
						switch (face_tracking)
						{
							case wivrn::from_headset::face_type::none:
								break;
							case wivrn::from_headset::face_type::fb2:
								application::get_fb_face_tracker2().get_weights(t0 + Δt, packet.face.emplace<wivrn::from_headset::tracking::fb_face2>());
								break;
							case wivrn::from_headset::face_type::htc: {
								auto & face_htc = packet.face.emplace<wivrn::from_headset::tracking::htc_face>();
								face_htc.eye_active = false;
								face_htc.lip_active = false;
								if (application::get_htc_face_tracking_eye_supported())
									application::get_htc_face_tracker_eye().get_weights(t0 + Δt, face_htc);
								if (application::get_htc_face_tracking_lip_supported())
									application::get_htc_face_tracker_lip().get_weights(t0 + Δt, face_htc);
								break;
							}
							case wivrn::from_headset::face_type::pico:
								application::get_pico_face_tracker().get_weights(t0 + Δt, packet.face.emplace<wivrn::from_headset::tracking::fb_face2>());
								break;
						}
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
					merged_tracking.emplace_back().interaction_profiles = {
					        interaction_profiles[0].load(),
					        interaction_profiles[1].load(),
					};
					current_size = 0;
				}
				current_size += size;
				merged_tracking.back().items.emplace_back(std::move(item));
			}

			packets.resize(std::max(packets.size(), merged_tracking.size() + hands.size() + body.size()));
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
			for (const auto & i: body)
			{
				if (i.poses)
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

	if (face_tracking == from_headset::face_type::pico)
		application::get_pico_face_tracker().stop();
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

static device_id derived_from(device_id target)
{
	switch (target)
	{
		case device_id::LEFT_AIM:
		case device_id::LEFT_PALM:
			return device_id::LEFT_GRIP;
		case device_id::RIGHT_AIM:
		case device_id::RIGHT_PALM:
			return device_id::RIGHT_GRIP;
		default:
			assert(false);
			__builtin_unreachable();
	}
}

void scenes::stream::on_interaction_profile_changed(const XrEventDataInteractionProfileChanged &)
{
	auto now = instance.now();
	for (device_id target: {
	             device_id::LEFT_AIM,
	             device_id::LEFT_PALM,
	             device_id::RIGHT_AIM,
	             device_id::RIGHT_PALM,
	     })
	{
		auto source = derived_from(target);
		auto source_space = application::space(device_to_space(source));
		auto target_space = application::space(device_to_space(target));

		if (not(source_space and target_space))
		{
			// This may happen if the runtime does not support palm ext
			// check if we have a device specific offset
			switch (guess_model())
			{
				case model::oculus_quest:
				case model::oculus_quest_2:
				case model::meta_quest_pro:
				case model::meta_quest_3:
				case model::meta_quest_3s:
					switch (target)
					{
						case device_id::LEFT_PALM:
						case device_id::RIGHT_PALM: {
							glm::quat q(glm::vec3(glm::radians(-60.), 0, 0));
							network_session->send_control(from_headset::derived_pose{
							        .source = source,
							        .target = target,
							        .relation = {
							                .orientation = {
							                        .x = q.x,
							                        .y = q.y,
							                        .z = q.z,
							                        .w = q.w,
							                },
							        },
							});
						}
						break;
						default:
							break;
					}
				default:
					break;
			}
		}
		else
		{
			if (auto pose = locate_space(target, target_space, source_space, now);
			    pose.flags & from_headset::tracking::position_valid and pose.flags & from_headset::tracking::orientation_valid)
			{
				network_session->send_control(from_headset::derived_pose{
				        .source = source,
				        .target = target,
				        .relation = pose.pose,
				});
			}
			else
			{
				// source == target means that the pose cannot be derived
				network_session->send_control(from_headset::derived_pose{
				        .source = target,
				        .target = target,
				});
			}
		}
	}

	std::array path = {
	        "/user/hand/left",
	        "/user/hand/right",
	};
#define DO_PROFILE(vendor, name)                                                \
	if (profile == "/interaction_profiles/" #vendor "/" #name)              \
	{                                                                       \
		interaction_profiles[i] = interaction_profile::vendor##_##name; \
		continue;                                                       \
	}

	for (size_t i = 0; i < 2; ++i)
	{
		try
		{
			auto profile = session.get_current_interaction_profile(path[i]);
			spdlog::info("interaction profile for {}: {}", path[i], profile);
			DO_PROFILE(khr, simple_controller)
			DO_PROFILE(bytedance, pico_neo3_controller)
			DO_PROFILE(bytedance, pico4_controller)
			DO_PROFILE(bytedance, pico4s_controller)
			DO_PROFILE(bytedance, pico_g3_controller)
			DO_PROFILE(google, daydream_controller)
			DO_PROFILE(hp, mixed_reality_controller)
			DO_PROFILE(htc, vive_controller)
			DO_PROFILE(htc, vive_cosmos_controller)
			DO_PROFILE(htc, vive_focus3_controller)
			DO_PROFILE(htc, vive_pro)
			DO_PROFILE(ml, ml2_controller)
			DO_PROFILE(microsoft, motion_controller)
			DO_PROFILE(microsoft, xbox_controller)
			DO_PROFILE(oculus, go_controller)
			DO_PROFILE(oculus, touch_controller)
			DO_PROFILE(meta, touch_pro_controller)
			DO_PROFILE(meta, touch_plus_controller)
			DO_PROFILE(meta, touch_controller_rift_cv1)
			DO_PROFILE(meta, touch_controller_quest_1_rift_s)
			DO_PROFILE(meta, touch_controller_quest_2)
			DO_PROFILE(samsung, odyssey_controller)
			DO_PROFILE(valve, index_controller)
		}
		catch (std::exception & e)
		{
			spdlog::warn("Failed to get current interation profile: {}", e.what());
		}
		interaction_profiles[i] = interaction_profile::none;
	}
}
