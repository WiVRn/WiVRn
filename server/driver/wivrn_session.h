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

#pragma once

#include "clock_offset.h"
#include "driver/app_pacer.h"
#include "utils/thread_safe.h"
#include "wivrn_connection.h"
#include "wivrn_controller.h"
#include "wivrn_hand_interaction.h"
#include "wivrn_hmd.h"
#include "wivrn_ipc.h"
#include "wivrn_packets.h"
#include "wivrn_uinput.h"
#include "xrt/xrt_results.h"
#include "xrt/xrt_system.h"
#include <atomic>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>

struct ipc_server;
struct u_system;
struct xrt_space_overseer;
struct xrt_system_compositor;
union xrt_session_event;

namespace wivrn
{
class wivrn_eye_tracker;
class wivrn_android_face_tracker;
class wivrn_fb_face2_tracker;
class wivrn_htc_face_tracker;
class wivrn_generic_tracker;
struct audio_device;
struct wivrn_comp_target;
struct wivrn_comp_target_factory;

class tracking_control_t
{
	using T = std::chrono::nanoseconds::rep;
	std::atomic<T> min;
	std::atomic<T> max;
	std::chrono::steady_clock::time_point next_sample;
	std::mutex mutex;
	decltype(to_headset::tracking_control::enabled) enabled;

public:
	tracking_control_t() :
	        next_sample(std::chrono::steady_clock::now())
	{
		enabled.fill(true);
	}
	void add(std::chrono::nanoseconds s)
	{
		auto sample = s.count();
		T prev = max;
		while (prev < sample and max.compare_exchange_weak(prev, sample))
		{
		}
		if (sample > 0)
		{
			prev = min;
			while (prev > sample and min.compare_exchange_weak(prev, sample))
			{
			}
		}
	}
	void send(wivrn_connection & connection, bool now = false);

	bool get_enabled(to_headset::tracking_control::id id);
	// Return true if value changed
	bool set_enabled(to_headset::tracking_control::id id, bool enabled);
};

class wivrn_session : public xrt_system_devices
{
	friend wivrn_comp_target_factory;
	std::unique_ptr<wivrn_connection> connection;
	pacing_app_factory app_pacers;

	u_system & xrt_system;
	ipc_server * mnd_ipc_server;
	xrt_space_overseer * space_overseer;
	xrt_system_compositor * system_compositor;

	std::mutex roles_mutex;
	xrt_system_roles roles{
	        .generation_id = 1,
	        .left = -1,
	        .right = -1,
	        .gamepad = -1,
	};

	tracking_control_t tracking_control;

	wivrn_hmd hmd;
	wivrn_controller left_controller;
	int32_t left_controller_index;
	wivrn_hand_interaction left_hand_interaction;
	int32_t left_hand_interaction_index;
	wivrn_controller right_controller;
	int32_t right_controller_index;
	wivrn_hand_interaction right_hand_interaction;
	int32_t right_hand_interaction_index;
	std::unique_ptr<wivrn_eye_tracker> eye_tracker;
	std::unique_ptr<wivrn_android_face_tracker> android_face_tracker;
	std::unique_ptr<wivrn_fb_face2_tracker> fb_face2_tracker;
	std::unique_ptr<wivrn_htc_face_tracker> htc_face_tracker;
	std::vector<std::unique_ptr<wivrn_generic_tracker>> generic_trackers;
	std::optional<wivrn_uinput> uinput_handler;

	std::shared_mutex comp_target_mutex;
	wivrn_comp_target * comp_target;

	clock_offset_estimator offset_est;

	std::mutex csv_mutex;
	std::ofstream feedback_csv;

	std::shared_ptr<audio_device> audio_handle;

	// when sessions shall be destroyed, key is timestap, value is client id
	thread_safe<std::map<int64_t, int32_t>> session_loss;

	std::jthread thread;

	wivrn_session(std::unique_ptr<wivrn_connection> connection, u_system &);

public:
	~wivrn_session();

	static xrt_result_t create_session(std::unique_ptr<wivrn_connection> connection,
	                                   u_system & system,
	                                   xrt_system_devices ** out_xsysd,
	                                   xrt_space_overseer ** out_xspovrs,
	                                   xrt_system_compositor ** out_xsysc);

	void start(ipc_server *);
	void stop();

	clock_offset get_offset();
	bool connected();
	const from_headset::headset_info_packet & get_info() const
	{
		return connection->info();
	};

	void unset_comp_target();

	wivrn_hmd & get_hmd()
	{
		return hmd;
	}

	void add_predict_offset(std::chrono::nanoseconds off)
	{
		tracking_control.add(off);
	}

	void set_enabled(to_headset::tracking_control::id id, bool enabled);
	void set_enabled(device_id id, bool enabled);
	void update_tracker_enabled();

	void operator()(from_headset::crypto_handshake &&) {}
	void operator()(from_headset::pin_check_1 &&) {}
	void operator()(from_headset::pin_check_3 &&) {}
	void operator()(from_headset::headset_info_packet &&);
	void operator()(from_headset::settings_request &&);
	void operator()(from_headset::settings_changed &&);
	void operator()(from_headset::handshake &&) {}
	void operator()(from_headset::trackings &&);
	void operator()(const from_headset::tracking &);
	void operator()(from_headset::derived_pose &&);
	void operator()(from_headset::hand_tracking &&);
	void operator()(from_headset::body_tracking &&);
	void operator()(from_headset::inputs &&);
	void operator()(from_headset::hid::input && e);
	void operator()(from_headset::timesync_response &&);
	void operator()(from_headset::feedback &&);
	void operator()(from_headset::battery &&);
	void operator()(from_headset::visibility_mask_changed &&);
	void operator()(from_headset::session_state_changed &&);
	void operator()(from_headset::user_presence_changed &&);
	void operator()(from_headset::refresh_rate_changed &&);
	void operator()(from_headset::override_foveation_center &&);
	void operator()(from_headset::get_application_list &&);
	void operator()(const from_headset::start_app &);
	void operator()(const from_headset::get_running_applications &);
	void operator()(const from_headset::set_active_application &);
	void operator()(const from_headset::stop_application &);
	void operator()(audio_data &&);

	void operator()(to_monado::stop &&);
	void operator()(to_monado::disconnect &&);
	void operator()(to_monado::set_bitrate &&);

	bool has_stream()
	{
		return connection->has_stream();
	}
	template <typename T>
	void send_stream(T && packet)
	{
		connection->send_stream(std::forward<T>(packet));
	}

	template <typename T>
	void send_control(T && packet)
	{
		connection->send_control(std::forward<T>(packet));
	}

	xrt_result_t push_event(const xrt_session_event &);

	void set_foveated_size(uint32_t width, uint32_t height);

	void dump_time(const std::string & event, uint64_t frame, int64_t time, uint8_t stream = -1, const char * extra = "");

private:
	void run(std::stop_token stop);
	void reconnect();

	void poll_session_loss();

	// xrt_system implementation
	xrt_result_t get_roles(xrt_system_roles * out_roles);
	xrt_result_t feature_inc(xrt_device_feature_type type);
	xrt_result_t feature_dec(xrt_device_feature_type type);
};

} // namespace wivrn
