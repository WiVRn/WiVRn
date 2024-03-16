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
#include "utils/named_thread.h"
#include "wivrn_client.h"
#include "xr/instance.h"

#include "spdlog/spdlog.h"
#include <aaudio/AAudio.h>
#include <cassert>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void wivrn::android::audio::input(AAudioStream * stream, const xrt::drivers::wivrn::to_headset::audio_stream_description::device & format)
{
	pthread_setname_np(pthread_self(), "audio_in_thread");

	try
	{
		AAudioStream_requestStart(stream);
		aaudio_stream_state_t state;
		aaudio_result_t result = AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_STARTING, &state, 1'000'000'000);
		if (result != AAUDIO_OK)
			throw std::runtime_error(std::string("Cannot start input stream: ") + AAudio_convertResultToText(result));

		xrt::drivers::wivrn::audio_data packet;
		const int frame_size = format.num_channels * sizeof(int16_t);
		// Try to make packets fit in a single TCP frame, and at most 10ms data
		const int max_frames = std::min<int>(
		        (format.sample_rate * 10) / 1000, // 10ms
		        1400 / frame_size);
		auto & buffer = packet.data.c;
		buffer.resize(frame_size * max_frames);

		while (!exiting)
		{
			result = AAudioStream_read(stream, buffer.data(), max_frames, 1'000'000'000);
			if (result > 0)
			{
				packet.timestamp = instance.now();
				packet.payload = std::span<uint8_t>(buffer.data(), frame_size * result);
				session.send_control(packet);
			}
		}
	}
	catch (std::exception & e)
	{
		spdlog::error("Error in audio input thread: {}", e.what());
		exit();
	}

	AAudioStream_close(stream);
}

void wivrn::android::audio::exit()
{
	exiting = true;
	if (speaker)
		AAudioStream_requestStop(speaker);
}

int32_t wivrn::android::audio::speaker_data_cb(AAudioStream * stream, void * userdata, void * audio_data_v, int32_t num_frames)
{
	auto self = (wivrn::android::audio *)userdata;
	uint8_t * audio_data = (uint8_t *)audio_data_v;

	if (self->exiting)
		return AAUDIO_CALLBACK_RESULT_STOP;

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
		}
		else
		{
			auto tmp = self->output_buffer.read();
			if (not tmp)
			{
				memset(audio_data, 0, num_frames * frame_size);
				return AAUDIO_CALLBACK_RESULT_CONTINUE;
			}
			self->speaker_tmp = std::move(*tmp);
		}
	}

	return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

wivrn::android::audio::audio(const xrt::drivers::wivrn::to_headset::audio_stream_description & desc, wivrn_session & session, xr::instance & instance) :
        session(session), instance(instance)
{
	AAudioStreamBuilder * builder;

	aaudio_result_t result = AAudio_createStreamBuilder(&builder);
	if (result != AAUDIO_OK)
	{
		::close(fd);
		throw std::runtime_error(std::string("Cannot create stream builder: ") + AAudio_convertResultToText(result));
	}

	if (desc.microphone)
	{
		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
		AAudioStreamBuilder_setSampleRate(builder, desc.microphone->sample_rate);
		AAudioStreamBuilder_setChannelCount(builder, desc.microphone->num_channels);
		AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
		AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

		AAudioStream * stream;
		result = AAudioStreamBuilder_openStream(builder, &stream);
		if (result != AAUDIO_OK)
			spdlog::error("Cannot create input stream: {}", AAudio_convertResultToText(result));
		else
			input_thread = utils::named_thread("audio_input_thread", &audio::input, this, stream, *desc.microphone);
	}

	if (desc.speaker)
	{
		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
		AAudioStreamBuilder_setSampleRate(builder, desc.speaker->sample_rate);
		AAudioStreamBuilder_setChannelCount(builder, desc.speaker->num_channels);
		AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
		AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_EXCLUSIVE);
		AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

		AAudioStreamBuilder_setDataCallback(builder, &speaker_data_cb, this);

		AAudioStream * stream;
		result = AAudioStreamBuilder_openStream(builder, &stream);
		if (result != AAUDIO_OK)
			spdlog::error("Cannot create output stream: {}", AAudio_convertResultToText(result));

		AAudioStream_requestStart(stream);
	}

	AAudioStreamBuilder_delete(builder);
}

wivrn::android::audio::~audio()
{
	exit();

	if (input_thread.joinable())
		input_thread.join();

	if (speaker)
	{
		aaudio_stream_state_t state = AAUDIO_STREAM_STATE_UNKNOWN;
		while (state != AAUDIO_STREAM_STATE_STOPPED)
		{
			state = AAudioStream_getState(speaker);
		}
		AAudioStream_close(speaker);
	}

	if (fd >= 0)
		::close(fd);
}

void wivrn::android::audio::operator()(xrt::drivers::wivrn::audio_data && data)
{
	output_buffer.write(std::move(data));
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
		stream = nullptr;
	}

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	result = AAudioStreamBuilder_openStream(builder, &stream);

	if (result == AAUDIO_OK)
	{
		info.microphone = {
		        .num_channels = (uint8_t)AAudioStream_getChannelCount(stream),
		        .sample_rate = (uint32_t)AAudioStream_getSampleRate(stream)};

		AAudioStream_close(stream);
		stream = nullptr;
	}

	AAudioStreamBuilder_delete(builder);
}
