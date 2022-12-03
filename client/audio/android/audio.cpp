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
#include "spdlog/spdlog.h"
#include <poll.h>
#include <cassert>
#include <aaudio/AAudio.h>
#include <pthread.h>
#include <unistd.h>
#include <netinet/tcp.h>


void wivrn::android::audio::output(AAudioStream * stream, const xrt::drivers::wivrn::to_headset::audio_stream_description::device& format)
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

		size_t offset = 0;
		char buffer[2000];
		const int frame_size = format.num_channels * sizeof(int16_t);

		while(!quit)
		{
			pollfd pfd{};
			pfd.fd = fd;
			pfd.events = POLLIN;

			int r = ::poll(&pfd, 1, 100);
			if (r < 0)
				throw std::system_error(errno, std::system_category());


			if (pfd.revents & (POLLHUP | POLLERR))
				throw std::runtime_error("Error on audio socket");

			if (pfd.revents & POLLIN)
			{
				ssize_t size = recv(fd, buffer + offset, sizeof(buffer) - offset, 0);
				int num_frames = (size + offset) / frame_size;

				num_frames = AAudioStream_write(stream, buffer, num_frames, 1'000'000'000);
				if (num_frames < 0)
					throw std::runtime_error(std::string("Cannot play output stream: ") + AAudio_convertResultToText(result));

				size_t consumed_bytes = num_frames * frame_size;
				memmove(buffer, buffer + consumed_bytes, size + offset - consumed_bytes);
				offset = size + offset - consumed_bytes;

				assert(offset >= 0);
				assert(offset < sizeof(buffer));
			}
		}
	}
	catch(std::exception& e)
	{
		spdlog::error("Error in audio output thread: {}", e.what());
		quit = true;
	}

	AAudioStream_close(stream);
}

void wivrn::android::audio::input(AAudioStream * stream, const xrt::drivers::wivrn::to_headset::audio_stream_description::device& format)
{
	pthread_setname_np(pthread_self(), "audio_in_thread");

	try
	{
		// while(!quit)
		// {
  //
		// }
	}
	catch(...)
	{
		quit = true;
	}

	AAudioStream_close(stream);
}

void wivrn::android::audio::init(const xrt::drivers::wivrn::to_headset::audio_stream_description & desc, sockaddr * address, socklen_t address_size)
{
	if (connect(fd, address, address_size) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

	int nodelay = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)
	{
		::close(fd);
		throw std::system_error{errno, std::generic_category()};
	}

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
			output_thread = std::thread(&audio::output, this, stream, *desc.speaker);
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
			input_thread = std::thread(&audio::input, this, stream, *desc.microphone);
	}

	AAudioStreamBuilder_delete(builder);
}

wivrn::android::audio::audio(const xrt::drivers::wivrn::to_headset::audio_stream_description & desc, const in_addr & address)
{
	fd = socket(AF_INET, SOCK_STREAM, 0);

	if (fd < 0)
		throw std::system_error{errno, std::generic_category()};

	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_addr = address;
	sa.sin_port = htons(desc.port);

	init(desc, (sockaddr *)&sa, sizeof(sa));
}

wivrn::android::audio::audio(const xrt::drivers::wivrn::to_headset::audio_stream_description & desc, const in6_addr & address)
{
	fd = socket(AF_INET6, SOCK_STREAM, 0);

	if (fd < 0)
		throw std::system_error{errno, std::generic_category()};

	sockaddr_in6 sa;
	sa.sin6_family = AF_INET6;
	sa.sin6_addr = address;
	sa.sin6_port = htons(desc.port);

	init(desc, (sockaddr *)&sa, sizeof(sa));
}

wivrn::android::audio::~audio()
{
	quit = true;

	if (input_thread.joinable())
		input_thread.join();

	if (output_thread.joinable())
		output_thread.join();

	if (fd >= 0)
		::close(fd);
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
			.sample_rate = (uint32_t)AAudioStream_getSampleRate(stream)
		};

		AAudioStream_close(stream);
		stream = nullptr;
	}

	AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_INPUT);
	result = AAudioStreamBuilder_openStream(builder, &stream);

	if (result == AAUDIO_OK)
	{
		info.microphone = {
			.num_channels = (uint8_t)AAudioStream_getChannelCount(stream),
			.sample_rate = (uint32_t)AAudioStream_getSampleRate(stream)
		};

		AAudioStream_close(stream);
		stream = nullptr;
	}

	AAudioStreamBuilder_delete(builder);
}

