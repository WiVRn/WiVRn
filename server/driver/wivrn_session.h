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
#include "wivrn_connection.h"
#include "wivrn_packets.h"
#include "xrt/xrt_results.h"
#include <atomic>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

class wivrn_hmd;
class wivrn_controller;
struct audio_device;

struct u_system;
struct xrt_system_devices;
struct xrt_space_overseer;
struct xrt_system_compositor;
struct wivrn_comp_target_factory;

namespace xrt::drivers::wivrn
{
struct wivrn_comp_target;

class max_accumulator
{
	using T = std::chrono::nanoseconds::rep;
	std::atomic<T> max;
	std::chrono::steady_clock::time_point next_sample;

public:
	max_accumulator() :
	        next_sample(std::chrono::steady_clock::now()) {}
	void add(std::chrono::nanoseconds s)
	{
		auto sample = s.count();
		T prev = max;
		while (prev < sample and max.compare_exchange_weak(prev, sample))
		{
		}
	}
	void send(wivrn_connection & connection);
};

class wivrn_session : public std::enable_shared_from_this<wivrn_session>
{
	friend wivrn_comp_target_factory;
	wivrn_connection connection;

	u_system & xrt_system;

	std::atomic<bool> quit = false;
	std::thread thread;

	std::unique_ptr<wivrn_hmd> hmd;
	std::unique_ptr<wivrn_controller> left_hand;
	std::unique_ptr<wivrn_controller> right_hand;
	wivrn_comp_target * comp_target;

	clock_offset_estimator offset_est;

	// offsets in ns of each requested frame from its generation time
	max_accumulator predict_offset;

	std::mutex csv_mutex;
	std::ofstream feedback_csv;

	std::shared_ptr<audio_device> audio_handle;

	wivrn_session(TCP && tcp, u_system &);

public:
	static xrt_result_t create_session(TCP && tcp,
	                                   u_system & system,
	                                   xrt_system_devices ** out_xsysd,
	                                   xrt_space_overseer ** out_xspovrs,
	                                   xrt_system_compositor ** out_xsysc);

	clock_offset get_offset();
	bool connected();

	void add_predict_offset(std::chrono::nanoseconds off)
	{
		predict_offset.add(off);
	}

	void operator()(from_headset::handshake &&) {}
	void operator()(from_headset::headset_info_packet &&);
	void operator()(from_headset::tracking &&);
	void operator()(from_headset::hand_tracking &&);
	void operator()(from_headset::inputs &&);
	void operator()(from_headset::timesync_response &&);
	void operator()(from_headset::feedback &&);
	void operator()(audio_data &&);

	template <typename T>
	void send_stream(T && packet)
	{
		connection.send_stream(std::forward<T>(packet));
	}

	template <typename T>
	void send_control(T && packet)
	{
		connection.send_control(std::forward<T>(packet));
	}

	std::array<to_headset::video_stream_description::foveation_parameter, 2> get_foveation_parameters();

	void dump_time(const std::string & event, uint64_t frame, uint64_t time, uint8_t stream = -1, const char * extra = "");

private:
	static void run(std::weak_ptr<wivrn_session>);
	void reconnect();
};

} // namespace xrt::drivers::wivrn
