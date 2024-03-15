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

void wivrn::android::audio::output(AAudioStream * stream, const xrt::drivers::wivrn::to_headset::audio_stream_description::device & format)
{
	pthread_setname_np(pthread_self(), "audio_out_thread");

	spdlog::info("audio_out_thread started");

	try
	{
		AAudioStream_requestStart(stream);
		aaudio_stream_state_t state;
		aaudio_result_t result = AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_STARTING, &state, 1'000'000'000);
		if (result != AAUDIO_OK)
			throw std::runtime_error(std::string("Cannot start output stream: ") + AAudio_convertResultToText(result));

		const size_t frame_size = format.num_channels * sizeof(int16_t);

		int32_t underruns = 0;
		int32_t frames = AAudioStream_getFramesPerBurst(stream);
		std::vector<uint8_t> silence(frame_size * frames, 0);

		// Tune buffer size to minimize latency
		{
			auto tuning_end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
			while (std::chrono::steady_clock::now() < tuning_end)
			{
				AAudioStream_write(stream, silence.data(), frames, 0);
				int32_t underrunCount = AAudioStream_getXRunCount(stream);
				if (underrunCount != underruns)
				{
					AAudioStream_setBufferSizeInFrames(stream, AAudioStream_getBufferSizeInFrames(stream) + frames);
					underruns = underrunCount;
				}
			}
		}

		while (!exiting)
		{
			auto packet = output_buffer.pop();
			auto & buffer = packet.payload;
			int num_frames = buffer.size() / frame_size;

			num_frames = AAudioStream_write(stream, buffer.data(), num_frames, 0);
			if (num_frames < 0)
				throw std::runtime_error(std::string("Cannot play output stream: ") + AAudio_convertResultToText(result));

			// Any remaining data that doesn't fit the buffer is discarded
		}
	}
	catch (std::exception & e)
	{
		spdlog::error("Error in audio output thread: {}", e.what());
		exit();
	}
	AAudioStream_close(stream);
}

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
	output_buffer.close();
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

	if (desc.speaker)
	{
		AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
		AAudioStreamBuilder_setSampleRate(builder, desc.speaker->sample_rate);
		AAudioStreamBuilder_setChannelCount(builder, desc.speaker->num_channels);
		AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
		AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);

		AAudioStream * stream;
		result = AAudioStreamBuilder_openStream(builder, &stream);
		if (result != AAUDIO_OK)
			spdlog::error("Cannot create output stream: {}", AAudio_convertResultToText(result));
		else
			output_thread = utils::named_thread("audio_output_thread", &audio::output, this, stream, *desc.speaker);
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

	AAudioStreamBuilder_delete(builder);
}

wivrn::android::audio::~audio()
{
	exit();

	if (input_thread.joinable())
		input_thread.join();

	if (output_thread.joinable())
		output_thread.join();

	if (fd >= 0)
		::close(fd);
}

void wivrn::android::audio::operator()(xrt::drivers::wivrn::audio_data && data)
{
	output_buffer.push(std::move(data));
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
