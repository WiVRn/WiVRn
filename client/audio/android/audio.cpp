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

#include "audio.h"

#include "application.h"
#include "jnipp.h"
#include "wivrn_client.h"
#include "xr/instance.h"

#include "spdlog/spdlog.h"
#include <aaudio/AAudio.h>

void wivrn::android::audio::exit()
{
	exiting = true;
	if (speaker)
	{
		speaker_stop_ack.wait(false);
		AAudioStream_requestStop(speaker);
	}
	if (microphone)
	{
		microphone_stop_ack.wait(false);
		AAudioStream_requestStop(microphone);
	}
}

int32_t wivrn::android::audio::speaker_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto self = (wivrn::android::audio *)userdata;
	uint8_t * audio_data = (uint8_t *)audio_data_v;

	if (self->exiting)
	{
		self->speaker_stop_ack = true;
		self->speaker_stop_ack.notify_all();
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	size_t frame_size = AAudioStream_getChannelCount(stream) * sizeof(uint16_t);

	while (num_frames != 0)
	{
		// remaining bytes in existing buffer
		ptrdiff_t tmp_remain = self->speaker_tmp.payload.size_bytes();
		// limit to requested frames
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
			if (auto tmp = self->output_buffer.read())
			{
				self->speaker_tmp = std::move(*tmp);
			}
			else
			{
				// Buffer underrun: add 5ms buffer
				size_t target_buffer_size = frame_size * AAudioStream_getSampleRate(stream) * 0.005;
#if __cpp_lib_shared_ptr_arrays >= 201707L
				self->speaker_tmp.data.c = std::make_shared<uint8_t[]>(target_buffer_size, 0);
#else
				self->speaker_tmp.data.c.reset(new uint8_t[target_buffer_size]());
#endif
				self->speaker_tmp.payload = std::span(self->speaker_tmp.data.c.get(), target_buffer_size);
				self->buffer_size_bytes += target_buffer_size;
				spdlog::info("Audio sync: underrun, add {} bytes buffer",
				             target_buffer_size);
			}
		}
	}

	// If we have more than 50ms of buffered data, discard some data
	if (self->buffer_size_bytes > frame_size * AAudioStream_getSampleRate(stream) * 0.05)
	{
		// discard excess data until we only have 30ms left
		size_t target_buffer_size = frame_size * AAudioStream_getSampleRate(stream) * 0.03;
		while (self->buffer_size_bytes > target_buffer_size and self->output_buffer.size() > 1)
		{
			auto tmp = self->output_buffer.read();
			if (not tmp)
				break;
			auto prev = self->buffer_size_bytes.fetch_sub(tmp->payload.size_bytes());
			spdlog::info("Audio sync: discard {} bytes (buffer {} target {})",
			             tmp->payload.size_bytes(),
			             prev,
			             target_buffer_size);
		}
	}

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

int32_t wivrn::android::audio::microphone_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto self = (wivrn::android::audio *)userdata;
	uint8_t * audio_data = (uint8_t *)audio_data_v;

	if (self->exiting)
	{
		self->microphone_stop_ack = true;
		self->microphone_stop_ack.notify_all();
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	size_t frame_size = AAudioStream_getChannelCount(stream) * sizeof(uint16_t);

	xrt::drivers::wivrn::audio_data packet{
	        .timestamp = self->instance.now(),
	        .payload = std::span(audio_data, frame_size * num_frames),
	};

	try
	{
		self->session.send_control(packet);
	}
	catch (...)
	{
		self->microphone_stop_ack = true;
		self->microphone_stop_ack.notify_all();
		return AAUDIO_CALLBACK_RESULT_STOP;
	}

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void wivrn::android::audio::build_microphone(AAudioStreamBuilder * builder, int32_t sample_rate, int32_t num_channels)
{
	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	AAudioStreamBuilder_setSampleRate(builder, sample_rate);
	AAudioStreamBuilder_setChannelCount(builder, num_channels);
	AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
	AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
	AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

	AAudioStreamBuilder_setDataCallback(builder, &microphone_data_cb, this);
	AAudioStreamBuilder_setErrorCallback(builder, &microphone_error_cb, this);

	aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &microphone);
	if (result != AAUDIO_OK)
		spdlog::error("Cannot create input stream: {}", AAudio_convertResultToText(result));

	result = AAudioStream_requestStart(microphone);
	if (result == AAUDIO_OK)
		spdlog::info("Microphone stream started");
	else
	{
		AAudioStream_close(microphone);
		spdlog::warn("Microphone stream failed to start: {}", AAudio_convertResultToText(result));
	}
}

void wivrn::android::audio::build_speaker(AAudioStreamBuilder * builder, int32_t sample_rate, int32_t num_channels)
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
		spdlog::error("Cannot create output stream: {}", AAudio_convertResultToText(result));

	AAudioStream_requestStart(speaker);
	if (result == AAUDIO_OK)
		spdlog::info("Speaker stream started");
	else
	{
		AAudioStream_close(speaker);
		spdlog::warn("Speaker stream failed to start: {}", AAudio_convertResultToText(result));
	}
}

void wivrn::android::audio::recreate_stream(audio * self, AAudioStreamStruct * stream)
{
	size_t num_channels = AAudioStream_getChannelCount(stream);
	size_t sample_rate = AAudioStream_getSampleRate(stream);

	AAudioStream_requestStop(stream);
	AAudioStream_close(stream);

	AAudioStreamBuilder * builder;
	aaudio_result_t result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK)
		throw std::runtime_error(std::string("Cannot create stream builder: ") + AAudio_convertResultToText(result));

	if (stream == self->speaker)
		self->build_speaker(builder, sample_rate, num_channels);
	else if (stream == self->microphone)
		self->build_microphone(builder, sample_rate, num_channels);
	else
		spdlog::error("Stream to recreate is neither speaker, not microphone!");

	AAudioStreamBuilder_delete(builder);
}

void wivrn::android::audio::speaker_error_cb(AAudioStream * stream, void * userdata, aaudio_result_t error)
{
	auto self = (wivrn::android::audio *)userdata;
	spdlog::warn("Speaker stream interrupted: {}", AAudio_convertResultToText(error));
	std::thread recreate_thread = utils::named_thread("recreate_stream", &self->recreate_stream, self, stream);
	recreate_thread.detach();
}

void wivrn::android::audio::microphone_error_cb(AAudioStream * stream, void * userdata, aaudio_result_t error)
{
	auto self = (wivrn::android::audio *)userdata;
	spdlog::warn("Microphone stream interrupted: {}", AAudio_convertResultToText(error));
	std::thread recreate_thread = utils::named_thread("recreate_stream", &self->recreate_stream, self, stream);
	recreate_thread.detach();
}

wivrn::android::audio::audio(const xrt::drivers::wivrn::to_headset::audio_stream_description & desc, wivrn_session & session, xr::instance & instance) :
        session(session), instance(instance)
{
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

wivrn::android::audio::~audio()
{
	exit();

	for (auto stream: {speaker, microphone})
	{
		if (stream)
			AAudioStream_close(stream);
	}
}

void wivrn::android::audio::operator()(xrt::drivers::wivrn::audio_data && data)
{
	auto size = data.payload.size_bytes();
	if (output_buffer.write(std::move(data)))
		buffer_size_bytes.fetch_add(size);
}

void wivrn::android::audio::get_audio_description(xrt::drivers::wivrn::from_headset::headset_info_packet & info)
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
		        .sample_rate = (uint32_t)AAudioStream_getSampleRate(stream)};

		AAudioStream_close(stream);
	}

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	result = AAudioStreamBuilder_openStream(builder, &stream);

	if (result == AAUDIO_OK)
	{
		info.microphone = {
		        .num_channels = 1, // Some headsets report 2 channels but then fail
		        .sample_rate = (uint32_t)AAudioStream_getSampleRate(stream),
		};

		AAudioStream_close(stream);
	}

	AAudioStreamBuilder_delete(builder);
}

void wivrn::android::audio::request_mic_permission()
{
	jni::object<""> act(application::native_app()->activity->clazz);
	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

	jni::string permission("android.permission.RECORD_AUDIO");
	auto result = ctx.call<jni::Int>("checkSelfPermission", permission);
	if (result != 0 /*PERMISSION_GRANTED*/)
	{
		spdlog::info("RECORD_AUDIO permission not granted, requesting it");
		jni::array permissions(permission);
		act.call<void>("requestPermissions", permissions, jni::Int(0));
	}
	else
	{
		spdlog::info("RECORD_AUDIO permission already granted");
	}
}
