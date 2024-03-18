/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "audio_pipewire.h"

#include "driver/wivrn_session.h"
#include "os/os_time.h"
#include "util/u_logging.h"
#include "utils/ring_buffer.h"
#include <memory>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

using namespace xrt::drivers::wivrn;

struct deleter
{
	void operator()(pw_main_loop * loop)
	{
		pw_main_loop_quit(loop);
		pw_main_loop_destroy(loop);
	}
	void operator()(pw_stream * stream)
	{
		pw_stream_destroy(stream);
	}
};

struct pipewire_device : public audio_device
{
	to_headset::audio_stream_description desc;
	wivrn_session & session;

	std::unique_ptr<pw_stream, deleter> speaker;
	pw_stream_events speaker_events{
	        .version = PW_VERSION_STREAM_EVENTS,
	        .process = &pipewire_device::speaker_process,
	};

	utils::ring_buffer<audio_data, 1000> mic_samples;
	audio_data mic_current;
	std::unique_ptr<pw_stream, deleter> microphone;
	pw_stream_events mic_events{
	        .version = PW_VERSION_STREAM_EVENTS,
	        .process = &pipewire_device::mic_process,
	};

	std::unique_ptr<pw_main_loop, deleter> pw_loop;

	std::thread thread;

	to_headset::audio_stream_description description() const override
	{
		return desc;
	};

	static void speaker_process(void * self_v);
	static void mic_process(void * self_v);

	void process_mic_data(xrt::drivers::wivrn::audio_data &&) override;

	~pipewire_device() = default;

	pipewire_device(
	        const std::string & source_name,
	        const std::string & source_description,
	        const std::string & sink_name,
	        const std::string & sink_description,
	        const xrt::drivers::wivrn::from_headset::headset_info_packet & info,
	        xrt::drivers::wivrn::wivrn_session & session) :
	        session(session)
	{
		int argc = 0;
		pw_init(&argc, nullptr);

		pw_loop.reset(pw_main_loop_new(nullptr));
		if (info.speaker)
		{
			desc.speaker = {
			        .num_channels = info.speaker->num_channels,
			        .sample_rate = info.speaker->sample_rate,
			};

			speaker.reset(pw_stream_new_simple(
			        pw_main_loop_get_loop(pw_loop.get()),
			        sink_name.c_str(),
			        pw_properties_new(
			                PW_KEY_NODE_NAME,
			                sink_name.c_str(),
			                PW_KEY_NODE_DESCRIPTION,
			                sink_description.c_str(),
			                PW_KEY_MEDIA_TYPE,
			                "Audio",
			                PW_KEY_MEDIA_CATEGORY,
			                "Capture",
			                PW_KEY_MEDIA_CLASS,
			                "Audio/Sink",
			                PW_KEY_MEDIA_ROLE,
			                "Game",
			                NULL),
			        &speaker_events,
			        this));

			std::vector<uint8_t> buffer(1024);
			spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), uint32_t(buffer.size()));

			spa_audio_info_raw info{
			        .format = SPA_AUDIO_FORMAT_S16,
			        .rate = desc.speaker->sample_rate,
			        .channels = desc.speaker->num_channels,
			};
			const spa_pod * param = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

			pw_stream_connect(
			        speaker.get(),
			        PW_DIRECTION_INPUT,
			        PW_ID_ANY,
			        pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
			        &param,
			        1);
			U_LOG_I("pipewire speaker stream created");
		}

		if (info.microphone)
		{
			desc.microphone = {
			        .num_channels = info.microphone->num_channels,
			        .sample_rate = info.microphone->sample_rate,
			};

			microphone.reset(pw_stream_new_simple(
			        pw_main_loop_get_loop(pw_loop.get()),
			        source_name.c_str(),
			        pw_properties_new(
			                PW_KEY_NODE_NAME,
			                source_name.c_str(),
			                PW_KEY_NODE_DESCRIPTION,
			                source_description.c_str(),
			                PW_KEY_MEDIA_TYPE,
			                "Audio",
			                PW_KEY_MEDIA_CATEGORY,
			                "Playback",
			                PW_KEY_MEDIA_CLASS,
			                "Audio/Source",
			                PW_KEY_MEDIA_ROLE,
			                "Game",
			                NULL),
			        &mic_events,
			        this));

			std::vector<uint8_t> buffer(1024);
			spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), uint32_t(buffer.size()));

			spa_audio_info_raw info{
			        .format = SPA_AUDIO_FORMAT_S16,
			        .rate = desc.microphone->sample_rate,
			        .channels = desc.microphone->num_channels,
			};
			const spa_pod * param = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

			pw_stream_connect(
			        microphone.get(),
			        PW_DIRECTION_OUTPUT,
			        PW_ID_ANY,
			        pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
			        &param,
			        1);
			U_LOG_I("pipewire microphone stream created");
		}

		if (desc.speaker or desc.microphone)
			thread = std::thread(
			        [loop = pw_loop.get()]() { pw_main_loop_run(loop); });
	}
};

void pipewire_device::mic_process(void * self_v)
{
	auto self = (pipewire_device *)self_v;
	auto buffer = pw_stream_dequeue_buffer(self->microphone.get());
	if (not buffer)
	{
		pw_log_warn("Out of buffers: %m");
		return;
	}

	const auto & data = buffer->buffer->datas[0];
	uint8_t* data_ptr = (uint8_t*)data.data;
	if (not data.data)
		return;

	const size_t frame_size = self->desc.microphone->num_channels * sizeof(int16_t);

	size_t num_frames = buffer->requested;
	if (num_frames == 0)
	{
		num_frames = data.maxsize / frame_size;
	}
	data.chunk->offset = 0;
	data.chunk->size = 0;
	data.chunk->stride = frame_size;

	while (num_frames != 0)
	{
		// remaining bytes in existing buffer
		auto& current = self->mic_current;
		ptrdiff_t tmp_remain = current.payload.size_bytes();
		// limit to requested frames
		tmp_remain = std::min<ptrdiff_t>(tmp_remain, num_frames * frame_size);
		if (tmp_remain)
		{
			memcpy(data_ptr, current.payload.data(), tmp_remain);
			current.payload = current.payload.subspan(tmp_remain);
			data_ptr += tmp_remain;
			data.chunk->size += tmp_remain;
			num_frames -= tmp_remain / frame_size;
		}
		else
		{
			auto tmp = self->mic_samples.read();
			if (not tmp)
				break;
			self->mic_current = std::move(*tmp);
		}
	}
	pw_stream_queue_buffer(self->microphone.get(), buffer);
}

void pipewire_device::speaker_process(void * self_v)
{
	auto self = (pipewire_device *)self_v;
	auto buffer = pw_stream_dequeue_buffer(self->speaker.get());
	if (not buffer)
	{
		pw_log_warn("Out of buffers: %m");
		return;
	}

	const auto & data = buffer->buffer->datas[0];
	if (not data.data)
		return;

	audio_data packet{
	        .timestamp = uint64_t(self->session.get_offset().to_headset(os_monotonic_get_ns()).count()),
	        .payload = std::span(
	                (uint8_t *)data.data + data.chunk->offset,
	                data.chunk->size),
	};
	try {
		self->session.send_control(packet);
	}
	catch (std::exception& e)
	{
		U_LOG_W("Failed to send audio data: %s", e.what());
	}
	pw_stream_queue_buffer(self->speaker.get(), buffer);
}

void pipewire_device::process_mic_data(xrt::drivers::wivrn::audio_data && sample)
{
	mic_samples.write(std::move(sample));
}

std::shared_ptr<audio_device> create_pipewire_handle(
        const std::string & source_name,
        const std::string & source_description,
        const std::string & sink_name,
        const std::string & sink_description,
        const xrt::drivers::wivrn::from_headset::headset_info_packet & info,
        wivrn_session & session)
{
	try
	{
		return std::make_shared<pipewire_device>(
		        source_name, source_description, sink_name, sink_description, info, session);
	}
	catch (std::exception & e)
	{
		U_LOG_I("Pipewire backend creation failed: %s", e.what());
		return nullptr;
	}
}
