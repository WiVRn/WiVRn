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
#include "xr/body_tracker.h"
#include "xr/face_tracker.h"
#include "xr/fb_body_tracker.h"
#include "xr/to_string.h"
#include <magic_enum.hpp>
#include <magic_enum_containers.hpp>
#include <ranges>
#include <spdlog/spdlog.h>
#include <thread>

#ifdef __ANDROID__
#include "android/battery.h"
#endif

static uint8_t cast_flags(XrSpaceLocationFlags location, XrSpaceVelocityFlags velocity)
{
	uint8_t flags = 0;
	if (location & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
		flags |= from_headset::tracking::orientation_valid;

	if (location & XR_SPACE_LOCATION_POSITION_VALID_BIT)
		flags |= from_headset::tracking::position_valid;

	if (velocity & XR_SPACE_VELOCITY_LINEAR_VALID_BIT)
		flags |= from_headset::tracking::linear_velocity_valid;

	if (velocity & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT)
		flags |= from_headset::tracking::angular_velocity_valid;

	if (location & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT)
		flags |= from_headset::tracking::orientation_tracked;

	if (location & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
		flags |= from_headset::tracking::position_tracked;

	return flags;
}

namespace
{

from_headset::tracking::pose locate_space(device_id device, XrSpace space, XrSpace reference, XrTime time)
{
	XrSpaceVelocity velocity{
	        .type = XR_TYPE_SPACE_VELOCITY,
	};

	XrSpaceLocation location{
	        .type = XR_TYPE_SPACE_LOCATION,
	        .next = &velocity,
	};

	auto res = xrLocateSpace(space, reference, time, &location);

	if (XR_SUCCEEDED(res))
		return {
		        .pose = location.pose,
		        .linear_velocity = velocity.linearVelocity,
		        .angular_velocity = velocity.angularVelocity,
		        .device = device,
		        .flags = cast_flags(location.locationFlags, velocity.velocityFlags),
		};
	spdlog::warn("xrLocateSpace failed for {}: {}", magic_enum::enum_name(device), xr::to_string(res));
	return {.device = device};
}

class locate_spaces_functor
{
	std::vector<XrSpaceLocationData> locations;
	std::vector<XrSpaceVelocityData> velocities;
	std::vector<wivrn::device_id> devices;
	std::vector<XrSpace> spaces;
	XrSpace reference;
	PFN_xrLocateSpaces locate_spaces = nullptr;

public:
	locate_spaces_functor(xr::instance & instance, XrSpace reference) :
	        reference(reference)
	{
		try
		{
			if (instance.get_api_version() >= XR_MAKE_VERSION(1, 1, 0))
				locate_spaces = instance.get_proc<PFN_xrLocateSpaces>("xrLocateSpaces");
			else
				locate_spaces = instance.get_proc<PFN_xrLocateSpacesKHR>("xrLocateSpacesKHR");
		}
		catch (std::exception & e)
		{
			spdlog::warn("Failed to load xrLocateSpaces function, fallback to xrLocateSpace");
		}
	}

	void add_space(wivrn::device_id device, XrSpace space, XrTime t, std::vector<from_headset::tracking::pose> & out)
	{
		if (locate_spaces)
		{
			// store, will be located later
			devices.push_back(device);
			spaces.push_back(space);
		}
		else
			out.push_back(locate_space(device, space, reference, t));
	}

	void resolve(
	        xr::session & session,
	        XrTime t,
	        std::vector<from_headset::tracking::pose> & out)
	{
		assert(devices.size() == spaces.size());
		if (devices.empty())
			return;
		if (locate_spaces)
		{
			locations.resize(spaces.size());
			velocities.resize(spaces.size());
			XrSpaceVelocities spc_velocities{
			        .type = XR_TYPE_SPACE_VELOCITIES,
			        .velocityCount = uint32_t(velocities.size()),
			        .velocities = velocities.data(),
			};
			XrSpaceLocations spc_locations{
			        .type = XR_TYPE_SPACE_LOCATIONS,
			        .next = &spc_velocities,
			        .locationCount = uint32_t(locations.size()),
			        .locations = locations.data(),
			};
			XrSpacesLocateInfo info{
			        .type = XR_TYPE_SPACES_LOCATE_INFO,
			        .baseSpace = reference,
			        .time = t,
			        .spaceCount = uint32_t(spaces.size()),
			        .spaces = spaces.data(),
			};
			auto res = locate_spaces(session, &info, &spc_locations);
			if (XR_SUCCEEDED(res))
			{
				for (size_t i = 0; i < devices.size(); ++i)
				{
					const auto & location = locations[i];
					const auto & velocity = velocities[i];

					out.push_back({
					        .pose = location.pose,
					        .linear_velocity = velocity.linearVelocity,
					        .angular_velocity = velocity.angularVelocity,
					        .device = devices[i],
					        .flags = cast_flags(location.locationFlags, velocity.velocityFlags),
					});
				}
			}
			else
				spdlog::warn("xrLocateSpaces failed: {}", xr::to_string(res));
			devices.clear();
			spaces.clear();
		}
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
			        .position = (*joints)[i].first.pose.position,
			        .orientation = pack((*joints)[i].first.pose.orientation),
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
		case device_id::LEFT_PINCH_POSE:
			return xr::spaces::pinch_left;
		case device_id::LEFT_POKE:
			return xr::spaces::poke_left;
		case device_id::RIGHT_AIM:
			return xr::spaces::aim_right;
		case device_id::RIGHT_GRIP:
			return xr::spaces::grip_right;
		case device_id::RIGHT_PALM:
			return xr::spaces::palm_right;
		case device_id::RIGHT_PINCH_POSE:
			return xr::spaces::pinch_right;
		case device_id::RIGHT_POKE:
			return xr::spaces::poke_right;
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

	magic_enum::containers::array<device_id, XrSpace> spaces{};

	const auto & config = application::get_config();

	{
		std::vector ids{
		        device_id::HEAD,
		        device_id::LEFT_AIM,
		        device_id::LEFT_GRIP,
		        device_id::LEFT_PALM,
		        device_id::RIGHT_AIM,
		        device_id::RIGHT_GRIP,
		        device_id::RIGHT_PALM,
		};
		if (instance.has_extension(XR_EXT_HAND_INTERACTION_EXTENSION_NAME))
		{
			spdlog::info("Adding hand_interaction poses to device list");
			ids.insert(ids.end(), {device_id::LEFT_PINCH_POSE, device_id::LEFT_POKE, device_id::RIGHT_PINCH_POSE, device_id::RIGHT_POKE});
		}

		if (config.check_feature(feature::eye_gaze))
			ids.emplace_back(device_id::EYE_GAZE);

		for (auto id: ids)
		{
			if (XrSpace space = application::space(device_to_space(id)))
				spaces[id] = space;
			else
				spdlog::warn("Missing space for device {}", magic_enum::enum_name(id));
		}
	}

	XrSpace view_space = application::space(xr::spaces::view);
	XrSpace world_space = application::space(xr::spaces::world);

	XrTime t0 = instance.now();
	from_headset::tracking tracking;
	std::vector<from_headset::hand_tracking> hands;
	std::vector<from_headset::body_tracking> body;
	std::vector<XrView> views;

	std::vector<serialization_packet> packets;

	const bool hand_tracking = config.check_feature(feature::hand_tracking);
	std::optional<xr::hand_tracker> left_hand;
	std::optional<xr::hand_tracker> right_hand;

	const bool face_tracking = config.check_feature(feature::face_tracking);
	xr::face_tracker face_tracker;

	const bool body_tracking = config.check_feature(feature::body_tracking);
	xr::body_tracker body_tracker;

	locate_spaces_functor locate_spaces{instance, world_space};

	on_interaction_profile_changed({});

	decltype(to_headset::tracking_control::pattern) pattern;
	size_t pattern_position = 0;

	XrDuration frame_duration;
	XrTime pattern_begin = instance.now();

	while (not exiting)
	{
		try
		{
			if (pattern_position == pattern.size())
			{
				// Upper limit to 200FPS
				frame_duration = std::max<XrDuration>(5'000'000, display_time_period);
				// Wait for next frame
				XrTime now = instance.now();
				pattern_begin = display_time_phase + (std::max(pattern_begin + frame_duration, now) / frame_duration) * frame_duration;
				std::this_thread::sleep_for(std::chrono::nanoseconds(pattern_begin - now));

				pattern_position = 0;
				// Check if a new pattern has been received
				if (auto locked = tracking_control.lock(); pattern.empty() or not locked->pattern.empty())
				{
					pattern.clear();
					std::swap(pattern, locked->pattern);
					pattern_position = 0;

					// Ensure head tracking is always done
					if (not std::ranges::contains(pattern, device_id::HEAD, &to_headset::tracking_control::sample::device))
						pattern.push_back({.device = device_id::HEAD});

					if (hand_tracking)
					{
						if (std::ranges::contains(pattern, device_id::LEFT_HAND, &to_headset::tracking_control::sample::device))
						{
							if (not left_hand)
								left_hand = session.create_hand_tracker(XR_HAND_LEFT_EXT);
						}
						else
							left_hand.reset();

						if (std::ranges::contains(pattern, device_id::RIGHT_HAND, &to_headset::tracking_control::sample::device))
						{
							if (not right_hand)
								right_hand = session.create_hand_tracker(XR_HAND_RIGHT_EXT);
						}
						else
							right_hand.reset();
					}

					if (face_tracking)
					{
						if (std::ranges::contains(pattern, device_id::FACE, &to_headset::tracking_control::sample::device))
						{
							if (std::holds_alternative<std::monostate>(face_tracker))
								face_tracker = xr::make_face_tracker(instance, system, session);
						}
						else
							face_tracker.emplace<std::monostate>();
					}

					if (body_tracking)
					{
						if (std::ranges::contains(pattern, device_id::BODY, &to_headset::tracking_control::sample::device))
						{
							if (std::holds_alternative<std::monostate>(body_tracker))
								body_tracker = xr::make_body_tracker(
								        instance,
								        system,
								        session,
								        application::get_generic_trackers(),
								        config.fb_lower_body,
								        config.fb_hip);
						}
						else
							body_tracker.emplace<std::monostate>();
					}

					std::ranges::sort(pattern, std::less{}, [frame_duration](const auto & i) { return -i.prediction_ns % frame_duration; });

					spdlog::info("Tracking pattern ({}µs):", frame_duration / 1'000);
					for (const auto & [device, pred]: pattern)
					{
						spdlog::info("\tt+{}µs {} ({}µs)",
						             (frame_duration - (pred % frame_duration)) / 1'000,
						             magic_enum::enum_name(device),
						             pred / 1'000);
					}
				}
			}

			hands.clear();
			body.clear();

			XrTime now = instance.now();
			XrTime t0 = pattern_begin + frame_duration - (pattern[pattern_position].prediction_ns % frame_duration);
			if (t0 > now + 500)
				std::this_thread::sleep_for(std::chrono::nanoseconds(t0 - now));

			bool interaction_profile_changed = this->interaction_profile_changed.exchange(false);

			if (interaction_profile_changed)
				if (auto htc = std::get_if<xr::htc_body_tracker>(&body_tracker))
					htc->update_active();

			tracking.interaction_profiles = {
			        interaction_profiles[0].load(),
			        interaction_profiles[1].load(),
			};

			tracking.production_timestamp = t0;
			tracking.timestamp = t0 + pattern[pattern_position].prediction_ns;
			tracking.view_flags = {};
			tracking.state_flags = {};
			tracking.views = {};
			tracking.device_poses.clear();
			tracking.face = {};

			if (recenter_requested.exchange(false))
				tracking.state_flags = wivrn::from_headset::tracking::recentered;

			try
			{
				for (; pattern_position < pattern.size(); ++pattern_position)
				{
					const auto & item = pattern[pattern_position];
					auto at_time = t0 + item.prediction_ns;

					// Don't merge items that are too far from the current one
					if (std::abs(tracking.timestamp - at_time) > 1'000'000)
						break;

					switch (item.device)
					{
						case device_id::HEAD:
							tracking.view_flags = session.locate_views(XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, tracking.timestamp, view_space, views);
							assert(views.size() == tracking.views.size());
							for (auto [i, j]: std::views::zip(views, tracking.views))
							{
								j.pose = i.pose;
								j.fov = i.fov;
							}
							locate_spaces.add_space(item.device, view_space, tracking.timestamp, tracking.device_poses);
							break;
						case wivrn::device_id::LEFT_GRIP:
						case wivrn::device_id::LEFT_AIM:
						case wivrn::device_id::LEFT_PALM:
						case wivrn::device_id::RIGHT_GRIP:
						case wivrn::device_id::RIGHT_AIM:
						case wivrn::device_id::RIGHT_PALM:
						case wivrn::device_id::LEFT_PINCH_POSE:
						case wivrn::device_id::LEFT_POKE:
						case wivrn::device_id::RIGHT_PINCH_POSE:
						case wivrn::device_id::RIGHT_POKE:
							locate_spaces.add_space(item.device, spaces[item.device], tracking.timestamp, tracking.device_poses);
							break;
						case wivrn::device_id::EYE_GAZE:
							// Eye gaze uses view pose as the origin
							switch (guess_model())
							{
								case model::pico_4_pro:
								case model::pico_4_enterprise: {
									// Pico headsets fail to locate gaze relative to view
									auto gaze = locate_space(item.device, spaces[item.device], world_space, tracking.timestamp);
									auto view_pose = locate_space(item.device, view_space, world_space, tracking.timestamp);
									glm::quat gaze_quat(gaze.pose.orientation.w, gaze.pose.orientation.x, gaze.pose.orientation.y, gaze.pose.orientation.z);
									glm::quat view_quat(view_pose.pose.orientation.w, view_pose.pose.orientation.x, view_pose.pose.orientation.y, view_pose.pose.orientation.z);
									gaze_quat = glm::conjugate(view_quat) * gaze_quat;
									using flags = from_headset::tracking::flags;
									tracking.device_poses.push_back(
									        from_headset::tracking::pose{
									                // Zero position and velocities
									                .pose = {
									                        .orientation = {
									                                .x = gaze_quat.x,
									                                .y = gaze_quat.y,
									                                .z = gaze_quat.z,
									                                .w = gaze_quat.w,
									                        },
									                },
									                .device = item.device,
									                .flags = uint8_t(gaze.flags & view_pose.flags & ~(flags::linear_velocity_valid | flags::angular_velocity_valid)),
									        });
								}
								break;
								default:
									tracking.device_poses.push_back(locate_space(item.device, spaces[item.device], spaces[wivrn::device_id::HEAD], tracking.timestamp));
									break;
							}
							break;
						case wivrn::device_id::FACE:
							std::visit(utils::overloaded{
							                   [](std::monostate &) {},
							                   [&](auto & ft) {
								                   ft.get_weights(at_time, tracking.face.emplace<typename std::remove_reference_t<decltype(ft)>::packet_type>());
							                   },
							           },
							           face_tracker);
							break;
						case wivrn::device_id::LEFT_HAND:
							if (left_hand)
							{
								hands.emplace_back(
								        t0,
								        at_time,
								        from_headset::hand_tracking::left,
								        locate_hands(*left_hand, world_space, tracking.timestamp));
							}
							break;
						case wivrn::device_id::RIGHT_HAND:
							if (right_hand)
							{
								hands.emplace_back(
								        t0,
								        at_time,
								        from_headset::hand_tracking::right,
								        locate_hands(*right_hand, world_space, tracking.timestamp));
							}
							break;
						case wivrn::device_id::BODY:
							std::visit(utils::overloaded{
							                   [](std::monostate &) {},
							                   [&](auto & b) {
								                   body.push_back(from_headset::body_tracking{
								                           .production_timestamp = tracking.production_timestamp,
								                           .timestamp = at_time,
								                           .poses = b.locate_spaces(at_time, world_space),
								                   });
							                   },
							           },
							           body_tracker);
							break;
						default:
							break;
					}
				}
				locate_spaces.resolve(session, tracking.timestamp, tracking.device_poses);
			}
			catch (const std::system_error & e)
			{
				if (e.code().category() != xr::error_category() or
				    e.code().value() != XR_ERROR_TIME_INVALID)
					throw;
			}

#ifdef __ANDROID__
			// FIXME: switch to event based
			if (next_battery_check < now)
			{
				auto status = get_battery_status();
				network_session->send_stream(from_headset::battery{
				        .charge = status.charge.value_or(-1),
				        .present = status.charge.has_value(),
				        .charging = status.charging,
				});

				next_battery_check = now + battery_check_interval;
			}
#endif

			packets.resize(std::max(packets.size(), 1 + hands.size() + body.size()));
			size_t packet_count = 0;

			if (not(tracking.device_poses.empty() and std::holds_alternative<std::monostate>(tracking.face)))
			{
				auto & packet = packets[packet_count++];
				packet.clear();
				wivrn_session::stream_socket_t::serialize(packet, tracking);
			}

			for (const auto & i: hands)
			{
				auto & packet = packets[packet_count++];
				packet.clear();
				wivrn_session::stream_socket_t::serialize(packet, i);
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
	*tracking_control.lock() = std::move(packet);
}

static device_id derived_from(device_id target)
{
	switch (target)
	{
		case device_id::LEFT_AIM:
		case device_id::LEFT_PALM:
		case device_id::LEFT_PINCH_POSE:
		case device_id::LEFT_POKE:
			return device_id::LEFT_GRIP;
		case device_id::RIGHT_AIM:
		case device_id::RIGHT_PALM:
		case device_id::RIGHT_PINCH_POSE:
		case device_id::RIGHT_POKE:
			return device_id::RIGHT_GRIP;
		default:
			assert(false);
			__builtin_unreachable();
	}
}

void scenes::stream::on_interaction_profile_changed(const XrEventDataInteractionProfileChanged &)
{
	interaction_profile_changed = true;
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
			DO_PROFILE(ext, hand_interaction_ext)
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

			// FIXME: remove once support for pre-1.1 profiles is dropped
			if (profile == "/interaction_profiles/facebook/touch_controller_pro")
			{
				interaction_profiles[i] = interaction_profile::meta_touch_pro_controller;
				continue;
			}
			if (profile == "/interaction_profiles/meta/touch_controller_plus")
			{
				interaction_profiles[i] = interaction_profile::meta_touch_plus_controller;
				continue;
			}
			spdlog::warn("unknown interaction profile {}", profile);
		}
		catch (std::exception & e)
		{
			spdlog::warn("Failed to get current interaction profile: {}", e.what());
		}
		interaction_profiles[i] = interaction_profile::none;
	}

	auto now = instance.now();
	for (device_id target: {
	             device_id::LEFT_AIM,
	             device_id::LEFT_PALM,
	             device_id::LEFT_PINCH_POSE,
	             device_id::LEFT_POKE,
	             device_id::RIGHT_AIM,
	             device_id::RIGHT_PALM,
	             device_id::RIGHT_PINCH_POSE,
	             device_id::RIGHT_POKE,
	     })
	{
		// don't do derived poses for hand interaction
		const bool right = (target >= device_id::RIGHT_GRIP && target <= device_id::RIGHT_PALM) || target == device_id::RIGHT_PINCH_POSE || target == device_id::RIGHT_POKE;
		if (interaction_profiles[right].load() == interaction_profile::ext_hand_interaction_ext)
		{
			network_session->send_control(from_headset::derived_pose{
			        .source = target,
			        .target = target,
			});
			continue;
		}

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
}
