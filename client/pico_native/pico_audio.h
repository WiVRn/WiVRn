#pragma once

#include "wivrn_packets.h"
#include "wivrn_client_pico.h"

#include <aaudio/AAudio.h>
#include <atomic>
#include <mutex>
#include <thread>

class pico_audio
{
	static int32_t speaker_data_cb(AAudioStream * stream, void * userdata, void * audio_data, int32_t num_frames);
	static int32_t microphone_data_cb(AAudioStream * stream, void * userdata, void * audio_data, int32_t num_frames);
	static void speaker_error_cb(AAudioStream * stream, void * userdata, aaudio_result_t error);
	static void microphone_error_cb(AAudioStream * stream, void * userdata, aaudio_result_t error);

	void build_speaker(AAudioStreamBuilder * builder, int32_t sample_rate, int32_t num_channels);
	void build_microphone(AAudioStreamBuilder * builder, int32_t sample_rate, int32_t num_channels);

	void recreate_stream(AAudioStream * stream);

	AAudioStream * speaker = nullptr;
	AAudioStream * microphone = nullptr;

	std::atomic<bool> speaker_stop_ack{false};
	std::atomic<bool> microphone_stop_ack{false};
	std::atomic<bool> mic_running{false};
	std::atomic<bool> exiting{false};

	wivrn_session_pico & session;

	std::mutex mutex;
	std::thread recreate_thread;

	wivrn::audio_data speaker_tmp;
	std::atomic<size_t> buffer_size_bytes{0};

	static constexpr size_t output_buffer_capacity = 100;
	wivrn::audio_data output_buffer_storage[output_buffer_capacity];
	std::atomic<size_t> output_read{0};
	std::atomic<size_t> output_write{0};

	void exit();

	int64_t get_timestamp_ns()
	{
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
	}

public:
	pico_audio(const wivrn::to_headset::audio_stream_description & desc, wivrn_session_pico & sess);
	~pico_audio();

	void operator()(wivrn::audio_data && data);
	void set_mic_state(bool running);

	static void get_audio_description(wivrn::from_headset::headset_info_packet & info);
};
