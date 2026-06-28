#include "pico_audio.h"

#include <spdlog/spdlog.h>
#include <cstring>
#include <algorithm>

void pico_audio::exit()
{
	exiting = true;
	if (speaker)
	{
		speaker_stop_ack.wait(false);
		AAudioStream_requestStop(speaker);
	}
	if (microphone && mic_running)
	{
		microphone_stop_ack.wait(false);
		AAudioStream_requestStop(microphone);
	}
}

int32_t pico_audio::speaker_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto * self = static_cast<pico_audio *>(userdata);
	uint8_t * audio_data = static_cast<uint8_t *>(audio_data_v);

	if (self->exiting)
	{
		self->speaker_stop_ack = true;
		self->speaker_stop_ack.notify_all();
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	size_t frame_size = AAudioStream_getChannelCount(stream) * sizeof(uint16_t);

	while (num_frames != 0)
	{
		ptrdiff_t tmp_remain = self->speaker_tmp.payload.size_bytes();
		tmp_remain = std::min<ptrdiff_t>(tmp_remain, num_frames * frame_size);
		if (tmp_remain)
		{
			memcpy(audio_data, self->speaker_tmp.payload.data(), tmp_remain);
			self->speaker_tmp.payload = self->speaker_tmp.payload.subspan(tmp_remain);
			audio_data += tmp_remain;
			num_frames -= tmp_remain / frame_size;
			self->buffer_size_bytes.fetch_sub(tmp_remain);
		}
		else
		{
			size_t r = self->output_read.load();
			size_t w = self->output_write.load();
			if (r != w)
			{
				size_t next_r = (r + 1) % output_buffer_capacity;
				self->speaker_tmp = std::move(self->output_buffer_storage[next_r]);
				self->output_read.store(next_r);
			}
			else
			{
				size_t target = frame_size * AAudioStream_getSampleRate(stream) * 0.005;
#if __cpp_lib_shared_ptr_arrays >= 201707L
				self->speaker_tmp.data.c = std::make_shared<uint8_t[]>(target, 0);
#else
				self->speaker_tmp.data.c.reset(new uint8_t[target]());
#endif
				self->speaker_tmp.payload = std::span(self->speaker_tmp.data.c.get(), target);
				self->buffer_size_bytes += target;
				// spdlog::debug("Audio underrun, add {} bytes", target);
			}
		}
	}

	if (self->buffer_size_bytes > frame_size * AAudioStream_getSampleRate(stream) * 0.05)
	{
		size_t target = frame_size * AAudioStream_getSampleRate(stream) * 0.03;
		while (self->buffer_size_bytes > target)
		{
			size_t r = self->output_read.load();
			size_t w = self->output_write.load();
			if (r == w)
				break;
			size_t next_r = (r + 1) % output_buffer_capacity;
			size_t sz = self->output_buffer_storage[next_r].payload.size_bytes();
			self->output_read.store(next_r);
			self->buffer_size_bytes.fetch_sub(sz);
		}
	}

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

int32_t pico_audio::microphone_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto * self = static_cast<pico_audio *>(userdata);
	uint8_t * audio_data = static_cast<uint8_t *>(audio_data_v);

	if (self->exiting)
	{
		self->microphone_stop_ack = true;
		self->microphone_stop_ack.notify_all();
		self->mic_running = false;
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	size_t frame_size = AAudioStream_getChannelCount(stream) * sizeof(uint16_t);

	thread_local std::vector<uint8_t> data_copy;
	data_copy.assign(audio_data, audio_data + frame_size * num_frames);

	try
	{
		self->session.send_control(wivrn::audio_data{
			.timestamp = self->get_timestamp_ns(),
			.payload = std::span(data_copy),
		});
	}
	catch (...)
	{
		self->microphone_stop_ack = true;
		self->microphone_stop_ack.notify_all();
		self->mic_running = false;
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void pico_audio::build_speaker(AAudioStreamBuilder * builder, int32_t sample_rate, int32_t num_channels)
{
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStreamBuilder_setSampleRate(builder, sample_rate);
	AAudioStreamBuilder_setChannelCount(builder, num_channels);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
	AAudioStreamBuilder_setDataCallback(builder, &speaker_data_cb, this);
	AAudioStreamBuilder_setErrorCallback(builder, &speaker_error_cb, this);

	aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &speaker);
	if (result != AAUDIO_OK)
		spdlog::error("Cannot create speaker stream: {}", AAudio_convertResultToText(result));

	AAudioStream_requestStart(speaker);
	if (result == AAUDIO_OK)
		spdlog::info("Speaker stream started: {}Hz, {}ch", sample_rate, num_channels);
	else
	{
		AAudioStream_close(speaker);
		speaker = nullptr;
	}
}

void pico_audio::build_microphone(AAudioStreamBuilder * builder, int32_t sample_rate, int32_t num_channels)
{
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	AAudioStreamBuilder_setSampleRate(builder, sample_rate);
	AAudioStreamBuilder_setChannelCount(builder, num_channels);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
#if __ANDROID_API__ >= 28
	AAudioStreamBuilder_setInputPreset(builder, AAUDIO_INPUT_PRESET_UNPROCESSED);
#endif
	AAudioStreamBuilder_setDataCallback(builder, &microphone_data_cb, this);
	AAudioStreamBuilder_setErrorCallback(builder, &microphone_error_cb, this);

	aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &microphone);
	if (result != AAUDIO_OK)
		spdlog::error("Cannot create microphone stream: {}", AAudio_convertResultToText(result));
	mic_running = false;
}

void pico_audio::recreate_stream(AAudioStream * stream)
{
	if (exiting)
		return;
	std::lock_guard lock(mutex);

	if (recreate_thread.joinable())
		recreate_thread.join();

	recreate_thread = std::thread([=, this]() {
		std::lock_guard lock(mutex);
		int32_t sample_rate = AAudioStream_getSampleRate(stream);
		int32_t num_channels = AAudioStream_getChannelCount(stream);

		AAudioStream_requestStop(stream);
		AAudioStream_close(stream);

		AAudioStreamBuilder * builder;
		AAudio_createStreamBuilder(&builder);

		if (stream == speaker)
			build_speaker(builder, sample_rate, num_channels);
		else if (stream == microphone)
			build_microphone(builder, sample_rate, num_channels);

		AAudioStreamBuilder_delete(builder);
	});
}

void pico_audio::speaker_error_cb(AAudioStream * stream, void * userdata, aaudio_result_t error)
{
	auto * self = static_cast<pico_audio *>(userdata);
	spdlog::warn("Speaker stream interrupted: {}", AAudio_convertResultToText(error));
	if (error == AAUDIO_ERROR_DISCONNECTED)
		self->recreate_stream(stream);
}

void pico_audio::microphone_error_cb(AAudioStream * stream, void * userdata, aaudio_result_t error)
{
	auto * self = static_cast<pico_audio *>(userdata);
	spdlog::warn("Microphone stream interrupted: {}", AAudio_convertResultToText(error));
	if (error == AAUDIO_ERROR_DISCONNECTED)
		self->recreate_stream(stream);
}

pico_audio::pico_audio(const wivrn::to_headset::audio_stream_description & desc, wivrn_session_pico & sess) :
	session(sess)
{
	std::lock_guard lock(mutex);
	AAudioStreamBuilder * builder;
	aaudio_result_t result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK)
		throw std::runtime_error(std::string("Cannot create stream builder: ") + AAudio_convertResultToText(result));

	if (desc.microphone)
		build_microphone(builder, desc.microphone->sample_rate, desc.microphone->num_channels);

	if (desc.speaker)
		build_speaker(builder, desc.speaker->sample_rate, desc.speaker->num_channels);

	AAudioStreamBuilder_delete(builder);
}

pico_audio::~pico_audio()
{
	exit();

	for (auto stream : {speaker, microphone})
	{
		if (stream)
			AAudioStream_close(stream);
	}

	std::lock_guard lock(mutex);
	if (recreate_thread.joinable())
		recreate_thread.join();
}

void pico_audio::operator()(wivrn::audio_data && data)
{
	auto size = data.payload.size_bytes();
	size_t w = output_write.load();
	size_t next_w = (w + 1) % output_buffer_capacity;
	size_t r = output_read.load();
	if (next_w == r)
		return;
	output_buffer_storage[next_w] = std::move(data);
	output_write.store(next_w);
	buffer_size_bytes.fetch_add(size);
}

void pico_audio::set_mic_state(bool running)
{
	if (!microphone)
		return;
	auto old = mic_running.exchange(running);
	if (old == running)
		return;
	if (running)
		AAudioStream_requestStart(microphone);
	else
		AAudioStream_requestStop(microphone);
}

void pico_audio::get_audio_description(wivrn::from_headset::headset_info_packet & info)
{
	AAudioStreamBuilder * builder;
	aaudio_result_t result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK)
		return;

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
	AAudioStream * stream;
	result = AAudioStreamBuilder_openStream(builder, &stream);
	if (result == AAUDIO_OK)
	{
		info.speaker = {
			.num_channels = (uint8_t)AAudioStream_getChannelCount(stream),
			.sample_rate = (uint32_t)AAudioStream_getSampleRate(stream),
		};
		AAudioStream_close(stream);
	}

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	AAudioStreamBuilder_setSampleRate(builder, 48000);
	result = AAudioStreamBuilder_openStream(builder, &stream);
	if (result != AAUDIO_OK)
	{
		AAudioStreamBuilder_setSampleRate(builder, AAUDIO_UNSPECIFIED);
		result = AAudioStreamBuilder_openStream(builder, &stream);
	}
	if (result == AAUDIO_OK)
	{
		info.microphone = {
			.num_channels = 1,
			.sample_rate = (uint32_t)AAudioStream_getSampleRate(stream),
		};
		AAudioStream_close(stream);
	}

	AAudioStreamBuilder_delete(builder);
}
