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

#include "utils/ring_buffer.h"
#include "wivrn_packets.h"
#include <atomic>
#include <mutex>
#include <thread>

struct AAudioStreamStruct;
struct AAudioStreamBuilderStruct;

class wivrn_session;

namespace xr
{
class instance;
}

namespace wivrn::android
{
class audio
{
	static int32_t speaker_data_cb(AAudioStreamStruct *, void *, void *, int32_t);
	static int32_t microphone_data_cb(AAudioStreamStruct *, void *, void *, int32_t);

	static void microphone_error_cb(AAudioStreamStruct *, void *, int32_t);
	static void speaker_error_cb(AAudioStreamStruct *, void *, int32_t);

	void recreate_stream(AAudioStreamStruct *);

	// must own the mutex to call the method
	void build_microphone(AAudioStreamBuilderStruct *, int32_t, int32_t);
	void build_speaker(AAudioStreamBuilderStruct *, int32_t, int32_t);

	utils::ring_buffer<wivrn::audio_data, 100> output_buffer;
	std::atomic<size_t> buffer_size_bytes;

	wivrn::audio_data speaker_tmp;
	AAudioStreamStruct * speaker = nullptr;
	std::atomic<bool> speaker_stop_ack = false;
	AAudioStreamStruct * microphone = nullptr;
	std::atomic<bool> microphone_stop_ack = false;
	std::atomic<bool> mic_running = false;

	wivrn_session & session;
	xr::instance & instance;

	std::mutex mutex;
	std::atomic<bool> exiting = false;
	std::thread recreate_thread;

	void exit();

public:
	audio(const audio &) = delete;
	audio & operator=(const audio &) = delete;
	audio(const wivrn::to_headset::audio_stream_description &, wivrn_session &, xr::instance &);
	~audio();

	void operator()(wivrn::audio_data &&);

	void set_mic_state(bool running);

	static void get_audio_description(wivrn::from_headset::headset_info_packet & info);
};
} // namespace wivrn::android
