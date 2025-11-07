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
#include <magic_enum.hpp>
#include <memory>
#include <pipewire/pipewire.h>
#include <pipewire/core.h>
#include <pipewire/context.h>
#include <pipewire/registry.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/result.h>
#include <unistd.h>
#include <cstring>
#include <thread>

namespace wivrn
{

namespace
{
struct deleter
{
	void operator()(pw_main_loop * loop)
	{
		pw_main_loop_destroy(loop);
	}
	void operator()(pw_stream * stream)
	{
		pw_stream_destroy(stream);
	}
	void operator()(pw_context * ctx)
	{
		pw_context_destroy(ctx);
	}
	void operator()(pw_core * core)
	{
		pw_core_disconnect(core);
	}
	void operator()(pw_registry * registry)
	{
		pw_proxy_destroy((pw_proxy *)registry);
	}
};

// Helper structure for registry callback
struct sink_find_state
{
	uint32_t node_id = PW_ID_ANY;
	bool found = false;
	const char * sink_name = nullptr;
};

// Helper function to set the default sink in PipeWire and return the previous default node ID
uint32_t set_default_sink_pipewire(const std::string & sink_name)
{
	pw_context * ctx = pw_context_new(nullptr, nullptr, 0);
	if (!ctx)
	{
		U_LOG_W("Failed to create PipeWire context for setting default sink");
		return;
	}

	pw_main_loop * loop = pw_main_loop_new(nullptr);
	if (!loop)
	{
		pw_context_destroy(ctx);
		U_LOG_W("Failed to create PipeWire main loop for setting default sink");
		return;
	}

	pw_core * core = pw_context_connect(ctx, nullptr, 0);
	if (!core)
	{
		pw_main_loop_destroy(loop);
		pw_context_destroy(ctx);
		U_LOG_W("Failed to connect to PipeWire daemon for setting default sink");
		return;
	}

	pw_registry * registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
	if (!registry)
	{
		pw_core_disconnect(core);
		pw_main_loop_destroy(loop);
		pw_context_destroy(ctx);
		U_LOG_W("Failed to get PipeWire registry for setting default sink");
		return;
	}

	// First, get the current default node before changing it
	uint32_t previous_default = PW_ID_ANY;
	
	// Query for the default audio sink node
	struct default_node_state
	{
		uint32_t node_id = PW_ID_ANY;
		bool found = false;
	};
	
	default_node_state default_state;
	pw_registry_listener default_listener{};
	default_listener.global = [](void * data, uint32_t id, uint32_t permissions, const char * type, uint32_t version, const struct spa_dict * props) {
		auto * state = static_cast<default_node_state *>(data);
		if (state->found)
			return;
		
		// Look for the default audio sink - it should have PW_KEY_NODE_DESCRIPTION or be marked as default
		const char * media_class = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : nullptr;
		if (media_class && strcmp(media_class, "Audio/Sink") == 0)
		{
			// Check if this is the default by looking at properties
			// The default node is typically the one that's currently active
			// We'll store the first Audio/Sink we find as a fallback
			if (state->node_id == PW_ID_ANY)
			{
				state->node_id = id;
			}
		}
	};
	
	pw_registry_add_listener(registry, &default_listener, &default_state);
	pw_core_sync(core, PW_ID_CORE, 0);
	
	// Get current default node - we need to query the core for this
	// Actually, PipeWire doesn't expose the default node directly through the core API
	// We'll need to use a different approach - query the default node from the session manager
	// For now, we'll try to get it from introspection or just note that we can't reliably get it
	// The best approach is to query the default node from pw-cli or use the metadata API
	
	// Try a simpler approach: query the default node from the core properties
	// But actually, the default node is managed by the session manager, not directly by core
	// We'll skip getting the previous default for now and just restore to a reasonable default
	// Or we can try to find any other Audio/Sink node as a fallback
	
	sink_find_state state;
	state.sink_name = sink_name.c_str();

	// Registry event callback to find our sink node
	pw_registry_listener registry_listener{};
	registry_listener.global = [](void * data, uint32_t id, uint32_t permissions, const char * type, uint32_t version, const struct spa_dict * props) {
		auto * state = static_cast<sink_find_state *>(data);
		if (state->found)
			return;

		const char * node_name = props ? spa_dict_lookup(props, PW_KEY_NODE_NAME) : nullptr;
		const char * media_class = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : nullptr;

		// Look for Audio/Sink nodes with our name
		if (node_name && media_class && strcmp(media_class, "Audio/Sink") == 0)
		{
			if (strcmp(node_name, state->sink_name) == 0)
			{
				state->node_id = id;
				state->found = true;
			}
		}
	};

	pw_registry_add_listener(registry, &registry_listener, &state);

	// Sync to get initial registry state
	pw_core_sync(core, PW_ID_CORE, 0);

	// Wait a bit for the registry to populate and find our node
	for (int i = 0; i < 50 && !state.found; i++)
	{
		pw_main_loop_iterate(loop, 0);
		usleep(10000); // 10ms
	}

	if (state.found && state.node_id != PW_ID_ANY)
	{
		// Before setting our node as default, try to find the current default
		// We'll look for other Audio/Sink nodes that might be the current default
		// This is a best-effort approach since PipeWire doesn't expose default node directly
		for (int i = 0; i < 10; i++)
		{
			pw_main_loop_iterate(loop, 0);
			usleep(10000);
		}
		
		// Try to find another Audio/Sink node as the previous default
		// We'll look for the first Audio/Sink that's not our own
		struct find_other_sink_state
		{
			uint32_t node_id = PW_ID_ANY;
			uint32_t our_node_id;
			bool found = false;
		};
		
		find_other_sink_state other_state;
		other_state.our_node_id = state.node_id;
		
		pw_registry_listener other_listener{};
		other_listener.global = [](void * data, uint32_t id, uint32_t permissions, const char * type, uint32_t version, const struct spa_dict * props) {
			auto * other_state = static_cast<find_other_sink_state *>(data);
			if (other_state->found)
				return;
			
			const char * media_class = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : nullptr;
			if (media_class && strcmp(media_class, "Audio/Sink") == 0 && id != other_state->our_node_id)
			{
				// Found another Audio/Sink node - use it as fallback
				other_state->node_id = id;
				other_state->found = true;
			}
		};
		
		pw_registry_add_listener(registry, &other_listener, &other_state);
		pw_core_sync(core, PW_ID_CORE, 0);
		
		for (int i = 0; i < 10 && !other_state.found; i++)
		{
			pw_main_loop_iterate(loop, 0);
			usleep(10000);
		}
		
		if (other_state.found)
		{
			previous_default = other_state.node_id;
			U_LOG_D("Found potential previous default PipeWire node: %u", previous_default);
		}
		
		int res = pw_core_set_default_node(core, state.node_id);
		if (res == 0)
		{
			U_LOG_I("Set WiVRn sink '%s' (node %u) as default PipeWire sink", sink_name.c_str(), state.node_id);
		}
		else
		{
			U_LOG_W("Failed to set default PipeWire sink: %s", spa_strerror(res));
		}
	}
		else
		{
			U_LOG_W("Could not find WiVRn sink '%s' in PipeWire registry to set as default", sink_name.c_str());
		}

	pw_proxy_destroy((pw_proxy *)registry);
	pw_core_disconnect(core);
	pw_main_loop_destroy(loop);
	pw_context_destroy(ctx);
	
	return previous_default;
}

struct pipewire_device : public audio_device
{
	to_headset::audio_stream_description desc;
	wivrn_session & session;

	std::unique_ptr<pw_main_loop, deleter> pw_loop;

	std::unique_ptr<pw_stream, deleter> speaker;
	pw_stream_events speaker_events{
	        .version = PW_VERSION_STREAM_EVENTS,
	        .process = &pipewire_device::speaker_process,
	};

	utils::ring_buffer<audio_data, 100> mic_samples;
	std::atomic<size_t> mic_buffer_size_bytes;
	audio_data mic_current;
	std::unique_ptr<pw_stream, deleter> microphone;
	pw_stream_events mic_events{
	        .version = PW_VERSION_STREAM_EVENTS,
	        .state_changed = &pipewire_device::mic_state_changed,
	        .process = &pipewire_device::mic_process,
	};
	std::jthread thread;
	
	uint32_t previous_default_node_id = PW_ID_ANY; // Store the previous default node to restore on disconnect

	to_headset::audio_stream_description description() const override
	{
		return desc;
	};

	static void speaker_process(void * self_v);
	static void mic_process(void * self_v);
	static void mic_state_changed(void * self_v, pw_stream_state old, pw_stream_state state, const char * error);

	void process_mic_data(wivrn::audio_data &&) override;

	~pipewire_device()
	{
		// Restore previous default node if we changed it
		if (previous_default_node_id != PW_ID_ANY)
		{
			// Restore in a separate thread to avoid blocking
			std::thread([node_id = previous_default_node_id]() {
				pw_context * ctx = pw_context_new(nullptr, nullptr, 0);
				if (!ctx)
				{
					U_LOG_W("Failed to create PipeWire context for restoring default node");
					return;
				}

				pw_main_loop * loop = pw_main_loop_new(nullptr);
				if (!loop)
				{
					pw_context_destroy(ctx);
					U_LOG_W("Failed to create PipeWire main loop for restoring default node");
					return;
				}

				pw_core * core = pw_context_connect(ctx, nullptr, 0);
				if (!core)
				{
					pw_main_loop_destroy(loop);
					pw_context_destroy(ctx);
					U_LOG_W("Failed to connect to PipeWire daemon for restoring default node");
					return;
				}

				// Check if the previous node still exists by querying the registry
				pw_registry * registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
				if (registry)
				{
					struct node_check_state
					{
						bool exists = false;
						uint32_t target_id;
					};
					
					node_check_state check_state;
					check_state.target_id = node_id;
					
					pw_registry_listener registry_listener{};
					registry_listener.global = [](void * data, uint32_t id, uint32_t permissions, const char * type, uint32_t version, const struct spa_dict * props) {
						auto * check_state = static_cast<node_check_state *>(data);
						if (id == check_state->target_id)
						{
							check_state->exists = true;
						}
					};
					
					pw_registry_add_listener(registry, &registry_listener, &check_state);
					pw_core_sync(core, PW_ID_CORE, 0);
					
					// Quick check
					for (int i = 0; i < 10; i++)
					{
						pw_main_loop_iterate(loop, 0);
						usleep(10000); // 10ms
					}
					
					if (check_state.exists)
					{
						int res = pw_core_set_default_node(core, node_id);
						if (res == 0)
						{
							U_LOG_I("Restored previous default PipeWire node: %u", node_id);
						}
						else
						{
							U_LOG_W("Failed to restore previous default PipeWire node: %s", spa_strerror(res));
						}
					}
					else
					{
						U_LOG_D("Previous default PipeWire node %u no longer exists, not restoring", node_id);
					}
					
					pw_proxy_destroy((pw_proxy *)registry);
				}
				
				pw_core_disconnect(core);
				pw_main_loop_destroy(loop);
				pw_context_destroy(ctx);
			}).detach();
		}
		
		pw_main_loop_quit(pw_loop.get());
	};

	pipewire_device(
	        const std::string & source_name,
	        const std::string & source_description,
	        const std::string & sink_name,
	        const std::string & sink_description,
	        const wivrn::from_headset::headset_info_packet & info,
	        wivrn::wivrn_session & session) :
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

			if (pw_stream_connect(
			            speaker.get(),
			            PW_DIRECTION_INPUT,
			            PW_ID_ANY,
			            pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
			            &param,
			            1) < 0)
				throw std::runtime_error("failed to connect speaker stream");
			U_LOG_I("pipewire speaker stream created");
			
			// Set this sink as the default output device
			// Do this in a separate thread to avoid blocking
			std::thread([this, sink_name]() {
				// Wait a bit for the node to be registered
				usleep(100000); // 100ms
				previous_default_node_id = set_default_sink_pipewire(sink_name);
			}).detach();
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

			if (pw_stream_connect(
			            microphone.get(),
			            PW_DIRECTION_OUTPUT,
			            PW_ID_ANY,
			            pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
			            &param,
			            1) < 0)
				throw std::runtime_error("failed to connect microphone stream");
			U_LOG_I("pipewire microphone stream created");
		}

		if (desc.speaker or desc.microphone)
			thread = std::jthread(
			        [this](std::stop_token) {
				pw_main_loop_run(pw_loop.get());
				speaker.reset();
				microphone.reset();
				; });
	}
};
} // namespace

void pipewire_device::mic_process(void * self_v)
{
	// std::cerr << "mic_process" << std::endl;
	auto self = (pipewire_device *)self_v;
	auto buffer = pw_stream_dequeue_buffer(self->microphone.get());
	if (not buffer)
	{
		U_LOG_W("Out of buffers: %s", strerror(errno));
		return;
	}

	const auto & data = buffer->buffer->datas[0];
	uint8_t * data_ptr = (uint8_t *)data.data;
	if (not data.data)
		return;

	const size_t frame_size = self->desc.microphone->num_channels * sizeof(int16_t);

#if PW_CHECK_VERSION(0, 3, 49)
	size_t num_frames = buffer->requested;
#else
	size_t num_frames = 0;
#endif
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
		auto & current = self->mic_current;
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
			self->mic_buffer_size_bytes -= tmp_remain;
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

	// discard excess data, so we don't accumulate latency
	size_t target_buffer_size = frame_size * self->desc.microphone->sample_rate * 0.08;
	while (self->mic_buffer_size_bytes > target_buffer_size and self->mic_samples.size() > 1)
	{
		auto tmp = self->mic_samples.read();
		if (not tmp)
			break;
		self->mic_buffer_size_bytes -= tmp->payload.size_bytes();
		U_LOG_D("Audio sync: discard %zd bytes", tmp->payload.size_bytes());
	}
}

void pipewire_device::mic_state_changed(void * self_v, pw_stream_state old, pw_stream_state state, const char * error)
{
	auto self = (pipewire_device *)self_v;
	switch (state)
	{
		case PW_STREAM_STATE_ERROR:
			U_LOG_W("Error on microphone stream: %s", error);
			return;
		case PW_STREAM_STATE_UNCONNECTED:
		case PW_STREAM_STATE_CONNECTING:
		case PW_STREAM_STATE_PAUSED:
			self->session.set_enabled(to_headset::tracking_control::id::microphone, false);
			return;
		case PW_STREAM_STATE_STREAMING:
			self->session.set_enabled(to_headset::tracking_control::id::microphone, true);
			return;
	}
}

void pipewire_device::speaker_process(void * self_v)
{
	auto self = (pipewire_device *)self_v;
	auto buffer = pw_stream_dequeue_buffer(self->speaker.get());
	if (not buffer)
	{
		U_LOG_W("Out of buffers: %s", strerror(errno));
		return;
	}

	const auto & data = buffer->buffer->datas[0];
	if (not data.data)
		return;

	try
	{
		self->session.send_control(audio_data{
		        .timestamp = self->session.get_offset().to_headset(os_monotonic_get_ns()),
		        .payload = std::span(
		                (uint8_t *)data.data + data.chunk->offset,
		                data.chunk->size),
		});
	}
	catch (std::exception & e)
	{
		U_LOG_D("Failed to send audio data: %s", e.what());
	}
	pw_stream_queue_buffer(self->speaker.get(), buffer);
}

void pipewire_device::process_mic_data(wivrn::audio_data && sample)
{
	auto size = sample.payload.size_bytes();
	if (mic_samples.write(std::move(sample)))
		mic_buffer_size_bytes += size;
}

std::shared_ptr<audio_device> create_pipewire_handle(
        const std::string & source_name,
        const std::string & source_description,
        const std::string & sink_name,
        const std::string & sink_description,
        const wivrn::from_headset::headset_info_packet & info,
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
} // namespace wivrn
