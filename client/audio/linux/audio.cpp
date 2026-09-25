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

#include "audio.h"

#include "utils/named_thread.h"
#include "wivrn_client.h"
#include "xr/instance.h"

#include "spdlog/spdlog.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wivrn::linux_audio
{
void pipewire_deleter::operator()(pw_main_loop * loop) const noexcept
{
	pw_main_loop_destroy(loop);
}

void pipewire_deleter::operator()(pw_thread_loop * loop) const noexcept
{
	pw_thread_loop_destroy(loop);
}

void pipewire_deleter::operator()(pw_stream * stream) const noexcept
{
	pw_stream_destroy(stream);
}

namespace
{
constexpr uint32_t default_sample_rate = 48'000;
constexpr uint32_t target_latency_ms = 5;

void init_pipewire()
{
	static bool done = [] {
		int argc = 0;
		pw_init(&argc, nullptr);
		return true;
	}();
}

void set_channel_positions(spa_audio_info_raw & info)
{
	switch (info.channels)
	{
		case 1:
			info.position[0] = SPA_AUDIO_CHANNEL_MONO;
			break;
		case 2:
			info.position[0] = SPA_AUDIO_CHANNEL_FL;
			info.position[1] = SPA_AUDIO_CHANNEL_FR;
			break;
		default:
			info.flags = SPA_AUDIO_FLAG_UNPOSITIONED;
			break;
	}
}

struct detected_devices
{
	bool pipewire = false;
	bool speaker = false;
	bool microphone = false;
};

struct device_probe
{
	pipewire_ptr<pw_main_loop> loop;
	int sync_seq = 0;
	detected_devices devices;

	static void global(
	        void * userdata,
	        uint32_t,
	        uint32_t,
	        const char * type,
	        uint32_t,
	        const spa_dict * props)
	{
		auto self = static_cast<device_probe *>(userdata);
		if (not type or not props or std::string_view(type) != PW_TYPE_INTERFACE_Node)
			return;

		const char * media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
		if (not media_class)
			return;

		if (std::string_view(media_class) == "Audio/Sink")
			self->devices.speaker = true;
		else if (std::string_view(media_class) == "Audio/Source")
			self->devices.microphone = true;
	}

	static void done(void * userdata, uint32_t id, int seq)
	{
		auto self = static_cast<device_probe *>(userdata);
		if (id == PW_ID_CORE and seq == self->sync_seq)
			pw_main_loop_quit(self->loop.get());
	}

	static void error(void * userdata, uint32_t, int, int, const char *)
	{
		auto self = static_cast<device_probe *>(userdata);
		pw_main_loop_quit(self->loop.get());
	}
};

const pw_registry_events probe_registry_events{
        .version = PW_VERSION_REGISTRY_EVENTS,
        .global = &device_probe::global,
};

const pw_core_events probe_core_events{
        .version = PW_VERSION_CORE_EVENTS,
        .done = &device_probe::done,
        .error = &device_probe::error,
};

detected_devices detect_devices()
{
	init_pipewire();

	device_probe probe;
	probe.loop.reset(pw_main_loop_new(nullptr));
	if (not probe.loop)
		return {};

	pw_context * context = pw_context_new(pw_main_loop_get_loop(probe.loop.get()), nullptr, 0);
	if (not context)
		return {};

	pw_core * core = pw_context_connect(context, nullptr, 0);
	if (not core)
	{
		pw_context_destroy(context);
		return {};
	}
	probe.devices.pipewire = true;

	pw_registry * registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
	if (not registry)
	{
		pw_core_disconnect(core);
		pw_context_destroy(context);
		return probe.devices;
	}

	spa_hook registry_listener{};
	spa_hook core_listener{};
	pw_registry_add_listener(registry, &registry_listener, &probe_registry_events, &probe);
	pw_core_add_listener(core, &core_listener, &probe_core_events, &probe);
	probe.sync_seq = pw_core_sync(core, PW_ID_CORE, 0);
	if (probe.sync_seq >= 0)
		pw_main_loop_run(probe.loop.get());

	spa_hook_remove(&core_listener);
	spa_hook_remove(&registry_listener);
	pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
	pw_core_disconnect(core);
	pw_context_destroy(context);

	return probe.devices;
}

std::string rate_string(uint32_t sample_rate)
{
	return std::format("1/{}", sample_rate);
}

std::string latency_string(uint32_t sample_rate)
{
	const uint32_t frames = std::max<uint32_t>(1, sample_rate * target_latency_ms / 1000);
	return std::format("{}/{}", frames, sample_rate);
}

pw_properties * make_properties(
        const char * category,
        const char * role,
        const char * node_name,
        const char * description,
        uint32_t sample_rate)
{
	auto rate = rate_string(sample_rate);
	auto latency = latency_string(sample_rate);
	return pw_properties_new(
	        PW_KEY_MEDIA_TYPE,
	        "Audio",
	        PW_KEY_MEDIA_CATEGORY,
	        category,
	        PW_KEY_MEDIA_ROLE,
	        role,
	        PW_KEY_NODE_NAME,
	        node_name,
	        PW_KEY_NODE_DESCRIPTION,
	        description,
	        PW_KEY_NODE_RATE,
	        rate.c_str(),
	        PW_KEY_NODE_LATENCY,
	        latency.c_str(),
	        nullptr);
}

void throw_connect_error(const char * name, int result)
{
	throw std::runtime_error(std::format("Failed to connect PipeWire {} stream: {}", name, spa_strerror(result)));
}
} // namespace

const pw_stream_events audio::speaker_events{
        .version = PW_VERSION_STREAM_EVENTS,
        .process = &audio::speaker_process,
};

const pw_stream_events audio::microphone_events{
        .version = PW_VERSION_STREAM_EVENTS,
        .process = &audio::microphone_process,
};

void audio::build_speaker(const to_headset::audio_stream_description::device & device)
{
	speaker.reset(pw_stream_new_simple(
	        pw_thread_loop_get_loop(loop.get()),
	        "WiVRn",
	        make_properties(
	                "Playback",
	                "Game",
	                "wivrn-client-speaker",
	                "WiVRn headset audio",
	                device.sample_rate),
	        &speaker_events,
	        this));
	if (not speaker)
		throw std::runtime_error("Failed to create PipeWire speaker stream");

	std::vector<uint8_t> buffer(1024);
	spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer.data(), uint32_t(buffer.size()));
	spa_audio_info_raw audio_info{
	        .format = SPA_AUDIO_FORMAT_S16,
	        .rate = device.sample_rate,
	        .channels = device.num_channels,
	};
	set_channel_positions(audio_info);

	const spa_pod * params[] = {
	        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audio_info),
	};

	const int result = pw_stream_connect(
	        speaker.get(),
	        PW_DIRECTION_OUTPUT,
	        PW_ID_ANY,
	        pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
	        params,
	        1);
	if (result < 0)
		throw_connect_error("speaker", result);

	spdlog::info(
	        "PipeWire speaker stream created: {} channels at {} Hz ({} ms target latency)",
	        device.num_channels,
	        device.sample_rate,
	        target_latency_ms);
}

void audio::build_microphone(const to_headset::audio_stream_description::device & device)
{
	microphone.reset(pw_stream_new_simple(
	        pw_thread_loop_get_loop(loop.get()),
	        "WiVRn",
	        make_properties(
	                "Capture",
	                "Communication",
	                "wivrn-client-microphone",
	                "WiVRn headset microphone",
	                device.sample_rate),
	        &microphone_events,
	        this));
	if (not microphone)
		throw std::runtime_error("Failed to create PipeWire microphone stream");

	std::vector<uint8_t> buffer(1024);
	spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer.data(), uint32_t(buffer.size()));
	spa_audio_info_raw audio_info{
	        .format = SPA_AUDIO_FORMAT_S16,
	        .rate = device.sample_rate,
	        .channels = device.num_channels,
	};
	set_channel_positions(audio_info);

	const spa_pod * params[] = {
	        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &audio_info),
	};

	const int result = pw_stream_connect(
	        microphone.get(),
	        PW_DIRECTION_INPUT,
	        PW_ID_ANY,
	        pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_INACTIVE | PW_STREAM_FLAG_MAP_BUFFERS),
	        params,
	        1);
	if (result < 0)
		throw_connect_error("microphone", result);

	spdlog::info(
	        "PipeWire microphone stream created: {} channels at {} Hz ({} ms target latency)",
	        device.num_channels,
	        device.sample_rate,
	        target_latency_ms);
}

audio::audio(
        const wivrn::to_headset::audio_stream_description & desc,
        wivrn_session & session,
        xr::instance & instance) :
        desc(desc), session(session), instance(instance)
{
	init_pipewire();

	loop.reset(pw_thread_loop_new("WiVRn audio", nullptr));
	if (not loop)
		throw std::runtime_error("Failed to create PipeWire thread loop");

	pw_thread_loop_lock(loop.get());
	const int start_result = pw_thread_loop_start(loop.get());
	if (start_result < 0)
	{
		pw_thread_loop_unlock(loop.get());
		loop.reset();
		throw std::runtime_error(std::format("Failed to start PipeWire thread loop: {}", spa_strerror(start_result)));
	}

	try
	{
		if (desc.speaker)
			build_speaker(*desc.speaker);
		if (desc.microphone)
			build_microphone(*desc.microphone);
	}
	catch (...)
	{
		speaker.reset();
		microphone.reset();
		pw_thread_loop_unlock(loop.get());
		pw_thread_loop_stop(loop.get());
		loop.reset();
		throw;
	}
	pw_thread_loop_unlock(loop.get());

	if (microphone)
		microphone_thread = utils::named_thread("wivrn-mic-send", &audio::microphone_sender, this);
}

audio::~audio()
{
	exiting = true;
	microphone_cv.notify_all();

	shutdown_pipewire();

	if (microphone_thread.joinable())
		microphone_thread.join();
}

void audio::shutdown_pipewire()
{
	if (not loop)
		return;

	pw_thread_loop_lock(loop.get());
	speaker.reset();
	microphone.reset();
	pw_thread_loop_unlock(loop.get());

	pw_thread_loop_stop(loop.get());
	loop.reset();
}

void audio::operator()(wivrn::audio_data && data)
{
	if (not desc.speaker)
		return;

	const auto size = data.payload.size_bytes();
	speaker_buffer_size_bytes.fetch_add(size, std::memory_order_relaxed);
	if (not speaker_samples.write(std::move(data)))
		speaker_buffer_size_bytes.fetch_sub(size, std::memory_order_relaxed);
}

void audio::set_mic_state(bool running)
{
	if (not microphone or not loop)
		return;

	const bool old = mic_running.exchange(running);
	if (old == running)
		return;

	pw_thread_loop_lock(loop.get());
	const int result = pw_stream_set_active(microphone.get(), running);
	pw_thread_loop_unlock(loop.get());

	if (result < 0)
	{
		mic_running = old;
		spdlog::warn("Failed to {} PipeWire microphone: {}", running ? "start" : "stop", spa_strerror(result));
	}
	microphone_cv.notify_one();
}

void audio::speaker_process(void * userdata)
{
	auto self = static_cast<audio *>(userdata);
	if (not self->speaker)
		return;

	pw_buffer * buffer = pw_stream_dequeue_buffer(self->speaker.get());
	if (not buffer)
		return;

	if (not buffer->buffer or buffer->buffer->n_datas == 0)
	{
		pw_stream_queue_buffer(self->speaker.get(), buffer);
		return;
	}

	auto & data = buffer->buffer->datas[0];
	if (not data.data or not data.chunk or not self->desc.speaker)
	{
		pw_stream_queue_buffer(self->speaker.get(), buffer);
		return;
	}

	const size_t frame_size = self->desc.speaker->num_channels * sizeof(int16_t);
	if (frame_size == 0)
	{
		pw_stream_queue_buffer(self->speaker.get(), buffer);
		return;
	}

	size_t num_frames = data.maxsize / frame_size;
	if (buffer->requested)
		num_frames = std::min<size_t>(num_frames, buffer->requested);

	const size_t requested_bytes = num_frames * frame_size;
	auto * output = static_cast<uint8_t *>(data.data);
	size_t written = 0;

	while (written < requested_bytes)
	{
		if (self->speaker_current.payload.empty())
		{
			auto next = self->speaker_samples.read();
			if (not next)
				break;
			self->speaker_current = std::move(*next);
		}

		const size_t count = std::min(self->speaker_current.payload.size_bytes(), requested_bytes - written);
		std::memcpy(output + written, self->speaker_current.payload.data(), count);
		self->speaker_current.payload = self->speaker_current.payload.subspan(count);
		written += count;
		self->speaker_buffer_size_bytes.fetch_sub(count, std::memory_order_relaxed);
	}

	if (written < requested_bytes)
		std::memset(output + written, 0, requested_bytes - written);

	data.chunk->offset = 0;
	data.chunk->stride = frame_size;
	data.chunk->size = requested_bytes;
	pw_stream_queue_buffer(self->speaker.get(), buffer);

	// more than 50 ms accumulated → drop queued packets until about 30 ms remain
	const size_t high_watermark = frame_size * self->desc.speaker->sample_rate * 50 / 1000;
	const size_t target = frame_size * self->desc.speaker->sample_rate * 30 / 1000;
	if (self->speaker_buffer_size_bytes.load(std::memory_order_relaxed) > high_watermark)
	{
		while (self->speaker_buffer_size_bytes.load(std::memory_order_relaxed) > target and self->speaker_samples.size() > 1)
		{
			auto dropped = self->speaker_samples.read();
			if (not dropped)
				break;
			self->speaker_buffer_size_bytes.fetch_sub(dropped->payload.size_bytes(), std::memory_order_relaxed);
		}
	}
}

void audio::microphone_process(void * userdata)
{
	auto self = static_cast<audio *>(userdata);
	if (not self->microphone)
		return;

	pw_buffer * buffer = pw_stream_dequeue_buffer(self->microphone.get());
	if (not buffer)
		return;

	if (not buffer->buffer or buffer->buffer->n_datas == 0)
	{
		pw_stream_queue_buffer(self->microphone.get(), buffer);
		return;
	}

	auto & data = buffer->buffer->datas[0];
	if (data.data and data.chunk and data.chunk->size != 0 and self->desc.microphone)
	{
		const size_t frame_size = self->desc.microphone->num_channels * sizeof(int16_t);
		const size_t offset = std::min<size_t>(data.chunk->offset, data.maxsize);
		size_t size = std::min<size_t>(data.chunk->size, data.maxsize - offset);
		if (frame_size != 0)
			size -= size % frame_size;

		if (size != 0)
		{
			wivrn::audio_data packet;
			packet.timestamp = self->instance.now();
			packet.data.c = std::make_shared_for_overwrite<uint8_t[]>(size);
			packet.payload = std::span(packet.data.c.get(), size);
			std::memcpy(packet.payload.data(), static_cast<uint8_t *>(data.data) + offset, size);

			self->microphone_buffer_size_bytes.fetch_add(size, std::memory_order_relaxed);
			if (self->microphone_samples.write(std::move(packet)))
				self->microphone_cv.notify_one();
			else
				self->microphone_buffer_size_bytes.fetch_sub(size, std::memory_order_relaxed);
		}
	}

	pw_stream_queue_buffer(self->microphone.get(), buffer);
}

void audio::microphone_sender()
{
	const size_t frame_size = desc.microphone ? desc.microphone->num_channels * sizeof(int16_t) : 0;
	const size_t high_watermark = desc.microphone ? frame_size * desc.microphone->sample_rate * 50 / 1000 : 0;
	const size_t target = desc.microphone ? frame_size * desc.microphone->sample_rate * 30 / 1000 : 0;

	while (not exiting)
	{
		if (not mic_running)
		{
			while (auto dropped = microphone_samples.read())
				microphone_buffer_size_bytes.fetch_sub(dropped->payload.size_bytes(), std::memory_order_relaxed);
		}
		else if (high_watermark != 0 and microphone_buffer_size_bytes.load(std::memory_order_relaxed) > high_watermark)
		{
			while (microphone_buffer_size_bytes.load(std::memory_order_relaxed) > target and microphone_samples.size() > 1)
			{
				auto dropped = microphone_samples.read();
				if (not dropped)
					break;
				microphone_buffer_size_bytes.fetch_sub(dropped->payload.size_bytes(), std::memory_order_relaxed);
			}
		}

		while (mic_running)
		{
			auto packet = microphone_samples.read();
			if (not packet)
				break;
			microphone_buffer_size_bytes.fetch_sub(packet->payload.size_bytes(), std::memory_order_relaxed);

			try
			{
				session.send_control(std::move(*packet));
			}
			catch (const std::exception & e)
			{
				if (not exiting)
					spdlog::debug("Failed to send microphone audio: {}", e.what());
				exiting = true;
				break;
			}
		}

		std::unique_lock lock(microphone_mutex);
		microphone_cv.wait_for(lock, std::chrono::milliseconds(20), [this]() {
			return exiting or microphone_samples.size() != 0;
		});
	}
}

void audio::get_audio_description(wivrn::from_headset::headset_info_packet & info)
{
	const auto devices = detect_devices();
	if (not devices.pipewire)
	{
		spdlog::warn("PipeWire is unavailable, disabling client audio");
		return;
	}

	if (devices.speaker)
	{
		info.speaker = {
		        .num_channels = 2,
		        .sample_rate = default_sample_rate,
		};
	}

	if (devices.microphone)
	{
		info.microphone = {
		        .num_channels = 1,
		        .sample_rate = default_sample_rate,
		};
	}
}
} // namespace wivrn::linux_audio
