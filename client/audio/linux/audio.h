/*
 * WiVRn VR streaming
 * Copyright (C) 2026  galister <galister-dev@pm.me>
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

#include "utils/ring_buffer.h"
#include "wivrn_packets.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>

struct pw_main_loop;
struct pw_stream;
struct pw_stream_events;
struct pw_thread_loop;

class wivrn_session;

namespace xr
{
class instance;
}

namespace wivrn::linux_audio
{
struct pipewire_deleter
{
	void operator()(pw_main_loop * loop) const noexcept;
	void operator()(pw_thread_loop * loop) const noexcept;
	void operator()(pw_stream * stream) const noexcept;
};

template <typename T>
using pipewire_ptr = std::unique_ptr<T, pipewire_deleter>;

class audio
{
	static void speaker_process(void * userdata);
	static void microphone_process(void * userdata);

	static const pw_stream_events speaker_events;
	static const pw_stream_events microphone_events;

	void build_speaker(const to_headset::audio_stream_description::device & device);
	void build_microphone(const to_headset::audio_stream_description::device & device);
	void microphone_sender();
	void shutdown_pipewire();

	to_headset::audio_stream_description desc;
	wivrn_session & session;
	xr::instance & instance;

	pipewire_ptr<pw_thread_loop> loop;
	pipewire_ptr<pw_stream> speaker;
	pipewire_ptr<pw_stream> microphone;

	utils::ring_buffer<wivrn::audio_data, 100> speaker_samples;
	std::atomic<size_t> speaker_buffer_size_bytes = 0;
	wivrn::audio_data speaker_current;

	utils::ring_buffer<wivrn::audio_data, 100> microphone_samples;
	std::atomic<size_t> microphone_buffer_size_bytes = 0;
	std::mutex microphone_mutex;
	std::condition_variable microphone_cv;
	std::atomic<bool> exiting = false;
	std::atomic<bool> mic_running = false;
	std::thread microphone_thread;

public:
	audio(const audio &) = delete;
	audio & operator=(const audio &) = delete;
	audio(const wivrn::to_headset::audio_stream_description &, wivrn_session &, xr::instance &);
	~audio();

	void operator()(wivrn::audio_data &&);
	void set_mic_state(bool running);

	static void get_audio_description(wivrn::from_headset::headset_info_packet & info);
};
} // namespace wivrn::linux_audio
