// Copyright 2022, Guillaume Meunier
// Copyright 2022, Patrick Nicolas
// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "wivrn_packets.h"
#include "wivrn_connection.h"
#include "xrt/xrt_system.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <fstream>

class wivrn_hmd;
class wivrn_controller;

namespace xrt::drivers::wivrn {

struct clock_offset
{
	std::chrono::nanoseconds epoch_offset{};

	uint64_t from_headset(uint64_t) const;

	std::chrono::nanoseconds
	to_headset(uint64_t timestamp_ns) const;
};

class wivrn_session : public std::enable_shared_from_this<wivrn_session>
{
	wivrn_connection connection;

	std::atomic<bool> quit = false;
	std::thread thread;
	std::mutex mutex;

	std::unique_ptr<wivrn_hmd> hmd;
	std::unique_ptr<wivrn_controller> left_hand;
	std::unique_ptr<wivrn_controller> right_hand;

	clock_offset offset;
	std::chrono::steady_clock::time_point offset_age{};

	std::ofstream feedback_csv;

	wivrn_session(TCP&& tcp, in6_addr &address);

public:
	static xrt_system_devices *
	create_session(TCP&& tcp);

	clock_offset
	get_offset();

	void
	operator()(from_headset::headset_info_packet &&);
	void
	operator()(from_headset::tracking &&);
	void
	operator()(from_headset::inputs &&);
	void
	operator()(from_headset::timesync_response &&);
	void
	operator()(from_headset::feedback &&);

	template <typename T>
	void
	send_stream(const T &packet)
	{
		connection.send_stream(packet);
	}

	template <typename T>
	void
	send_control(const T &packet)
	{
		connection.send_control(packet);
	}

private:
	static void run(std::weak_ptr<wivrn_session>);
};

} // namespace xrt::drivers::wivrn
