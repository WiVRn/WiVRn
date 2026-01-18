#include "audio_pulse.h"

#include "../wivrn_ipc.h"
#include "driver/wivrn_session.h"
#include "os/os_time.h"
#include "util/u_logging.h"
#include "utils/sync_queue.h"
#include "utils/wrap_lambda.h"

#include <pulse/context.h>
#include <pulse/ext-device-manager.h>
#include <pulse/introspect.h>
#include <pulse/proplist.h>
#include <pulse/thread-mainloop.h>

#include <atomic>
#include <fcntl.h>
#include <filesystem>
#include <future>
#include <iostream>
#include <sys/poll.h>

static const char * source_name = "WiVRn-mic";
static const char * sink_name = "WiVRn";
static const char * source_pipe = "wivrn-source";
static const char * sink_pipe = "wivrn-sink";

namespace wivrn
{

struct module_entry
{
	uint32_t module;
	uint32_t device;
	std::filesystem::path socket;
};

void wait_connected(std::atomic<pa_context_state> & state)
{
	while (true)
	{
		auto current = state.load();
		switch (current)
		{
			case PA_CONTEXT_READY:
				return;
			case PA_CONTEXT_FAILED:
				throw std::runtime_error("connection failed");
			default:
				state.wait(current);
		}
	}
}

void unload_module(uintptr_t id);

void unload_module(pa_context * ctx, uint32_t id)
{
	std::promise<void> p;
	wrap_lambda cb = [&p, id](pa_context *, int success) {
		if (not success)
			std::cerr << "failed to unload pulseaudio module " << id
			          << std::endl;
		else
			std::cerr << "pulseaudio module " << id << " unloaded"
			          << std::endl;
		p.set_value();
	};
	auto op = pa_context_unload_module(ctx, id, cb, cb);
	pa_operation_unref(op);
	p.get_future().get();

	remove_cleanup_function(unload_module, id);
}

std::optional<module_entry> get_sink(pa_context * ctx, const char * name)
{
	std::optional<module_entry> result;
	std::promise<void> p;
	wrap_lambda cb(
	        [&result, &p](pa_context *, const pa_sink_info * i, int eol) {
		        if (eol)
		        {
			        p.set_value();
			        return;
		        }
		        result = {.module = i->owner_module, .device = i->index};
	        });
	auto op = pa_context_get_sink_info_by_name(ctx, name, cb, cb);
	pa_operation_unref(op);
	p.get_future().wait();
	return result;
}

std::optional<module_entry> get_source(pa_context * ctx, const char * name)
{
	std::optional<module_entry> result;
	std::promise<void> p;
	wrap_lambda cb(
	        [&result, &p](pa_context *, const pa_source_info * i, int eol) {
		        if (eol)
		        {
			        p.set_value();
			        return;
		        }
		        result = {.module = i->owner_module, .device = i->index};
	        });
	auto op = pa_context_get_source_info_by_name(ctx, name, cb, cb);
	pa_operation_unref(op);
	p.get_future().wait();
	return result;
}

std::filesystem::path get_socket_path()
{
	const char * path = std::getenv("XDG_RUNTIME_DIR");
	if (path)
		return path;
	path = "/tmp/wivrn";
	std::filesystem::create_directories(path);
	U_LOG_W("XDG_RUNTIME_DIR is not set, using %s instead", path);
	return path;
}

module_entry ensure_sink(pa_context * ctx, const char * name, const std::string & description, int channels, int sample_rate)
{
	auto sink = get_sink(ctx, name);
	if (sink)
		unload_module(ctx, sink->module);

	std::promise<uint32_t> module_index;
	wrap_lambda cb = [&module_index](pa_context *, uint32_t index) {
		module_index.set_value(index);
	};
	std::filesystem::path fifo = get_socket_path() / sink_pipe;
	std::stringstream params;
	params << "sink_name=" << std::quoted(name)
	       << " file=" << std::quoted(fifo.string())
	       << " channels=" << channels
	       << " rate=" << sample_rate
	       << " use_system_clock_for_timing=yes"
	       << " sink_properties=" << PA_PROP_DEVICE_DESCRIPTION << "=" << std::quoted(description)
	       << PA_PROP_DEVICE_ICON_NAME << "=network-wireless";
	;
	auto op = pa_context_load_module(ctx, "module-pipe-sink", params.str().c_str(), cb, cb);
	pa_operation_unref(op);
	module_index.get_future().wait();

	sink = get_sink(ctx, name);
	if (not sink)
		throw std::runtime_error("failed to create audio sink " + std::string(name));
	sink->socket = fifo;

	add_cleanup_function(unload_module, sink->module);

	return *sink;
}

module_entry ensure_source(pa_context * ctx, const char * name, const std::string & description, int channels, int sample_rate)
{
	auto source = get_source(ctx, name);
	if (source)
		unload_module(ctx, source->module);

	std::promise<uint32_t> module_index;
	wrap_lambda cb = [&module_index](pa_context *, uint32_t index) {
		module_index.set_value(index);
	};
	std::filesystem::path fifo = get_socket_path() / source_pipe;
	std::stringstream params;
	params << "source_name=" << std::quoted(name)
	       << " file=" << std::quoted(fifo.string())
	       << " channels=" << channels
	       << " rate=" << sample_rate
	       << " source_properties=" << PA_PROP_DEVICE_DESCRIPTION << "=" << std::quoted(description)
	       << PA_PROP_DEVICE_ICON_NAME << "=network-wireless";
	;
	auto op = pa_context_load_module(ctx, "module-pipe-source", params.str().c_str(), cb, cb);
	pa_operation_unref(op);
	module_index.get_future().wait();

	source = get_source(ctx, name);
	if (not source)
		throw std::runtime_error("failed to create audio source " + std::string(name));
	source->socket = fifo;

	add_cleanup_function(unload_module, source->module);

	return *source;
}

struct pa_deleter
{
	void operator()(pa_threaded_mainloop * loop)
	{
		pa_threaded_mainloop_stop(loop);
		pa_threaded_mainloop_free(loop);
	}
	void operator()(pa_context * ctx)
	{
		pa_context_unref(ctx);
	}
};

class pa_connection
{
	std::unique_ptr<pa_threaded_mainloop, pa_deleter> main_loop;
	std::unique_ptr<pa_context, pa_deleter> ctx;
	std::atomic<pa_context_state> context_state{PA_CONTEXT_UNCONNECTED};

public:
	pa_connection(const char * app_name)
	{
		main_loop.reset(pa_threaded_mainloop_new());
		auto loop = pa_threaded_mainloop_get_api(main_loop.get());
		ctx.reset(pa_context_new(loop, app_name));

		wrap_lambda context_state_cb = [this](pa_context * c) {
			context_state.store(pa_context_get_state(c));
			context_state.notify_all();
		};
		pa_context_set_state_callback(ctx.get(), context_state_cb, context_state_cb);

		int ret = pa_context_connect(ctx.get(), nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr);
		if (ret < 0)
			throw std::runtime_error(
			        "failed to setup pulseaudio connection "
			        "(pa_context_connect)");

		ret = pa_threaded_mainloop_start(main_loop.get());
		if (ret < 0)
			throw std::runtime_error(
			        "failed to setup pulseaudio connection "
			        "(pa_threaded_mainloop_start)");
		wait_connected(context_state);
	}

	operator pa_context *()
	{
		return ctx.get();
	}
};

struct pulse_device : public audio_device
{
	wivrn::to_headset::audio_stream_description desc;

	std::thread mic_thread;
	std::thread speaker_thread;
	std::atomic<bool> quit;

	std::optional<module_entry> speaker;
	std::optional<module_entry> microphone;

	utils::sync_queue<audio_data> mic_buffer;

	wivrn::fd_base speaker_pipe;
	wivrn::fd_base mic_pipe;

	wivrn::wivrn_session & session;

	~pulse_device()
	{
		quit = true;
		mic_buffer.close();
		if (mic_thread.joinable())
			mic_thread.join();
		if (speaker_thread.joinable())
			speaker_thread.join();
		if (speaker or microphone)
		{
			try
			{
				pa_connection cnx("WiVRn");
				if (speaker)
					unload_module(cnx, speaker->module);
				if (microphone)
					unload_module(cnx, microphone->module);
			}
			catch (const std::exception & e)
			{
				std::cerr << "failed to depublish pulseaudio modules: "
				          << e.what() << std::endl;
			}
		}
	}

	wivrn::to_headset::audio_stream_description description() const override
	{
		return desc;
	};

	void run_speaker()
	{
		assert(desc.speaker);
		pthread_setname_np(pthread_self(), "speaker_thread");

		U_LOG_I("started speaker thread, sample rate %dHz, %d channels", desc.speaker->sample_rate, desc.speaker->num_channels);

		const size_t sample_size = desc.speaker->num_channels * sizeof(int16_t);
		// use buffers of up to 2ms
		// read buffers must be smaller than buffer size on client or we will discard chunks often
		const size_t buffer_size = (desc.speaker->sample_rate * sample_size * 2) / 1000;
		std::vector<uint8_t> buffer(buffer_size, 0);
		size_t remainder = 0;

		// Flush existing data, but keep alignment
		{
			char sewer[1024];
			while (true)
			{
				int size = read(speaker_pipe.get_fd(), sewer, sizeof(sewer));
				if (size <= 0)
					break;
				remainder = (remainder + size) % sample_size;
			}
		}

		try
		{
			while (not quit)
			{
				pollfd pfd{};
				pfd.fd = speaker_pipe.get_fd();
				pfd.events = POLL_IN;

				int r = poll(&pfd, 1, 100);
				if (r < 0)
					throw std::system_error(errno, std::system_category());
				if (pfd.revents & (POLLHUP | POLLERR))
					throw std::runtime_error("Error on speaker pipe");
				if (pfd.revents & POLLIN)
				{
					int size = read(pfd.fd, buffer.data() + remainder, buffer_size - remainder);
					if (size < 0)
						throw std::system_error(errno, std::system_category());
					size += remainder;              // full size of available data
					remainder = size % sample_size; // data to keep for next iteration
					size -= remainder;              // size of data to send

					try
					{
						session.send_control(wivrn::audio_data{
						        .timestamp = session.get_offset().to_headset(os_monotonic_get_ns()),
						        .payload = std::span<uint8_t>(buffer.begin(), size),
						});
					}
					catch (std::exception & e)
					{
						U_LOG_D("Failed to send audio data: %s", e.what());
					}

					// put the remaining data at the beginning of the buffer
					memmove(buffer.data(), buffer.data() + size, remainder);
				}
			}
		}
		catch (const std::exception & e)
		{
			U_LOG_D("Error in audio thread: %s", e.what());
		}
	}

	void run_mic()
	{
		assert(desc.microphone);
		pthread_setname_np(pthread_self(), "mic_thread");

		const size_t sample_size = desc.microphone->num_channels * sizeof(int16_t);
		try
		{
			while (not quit)
			{
				pollfd pfd{};
				pfd.fd = mic_pipe.get_fd();
				pfd.events = POLLOUT;

				int r = poll(&pfd, 1, 100);
				if (r < 0)
					throw std::system_error(errno, std::system_category());
				if (pfd.revents & (POLLHUP | POLLERR))
					throw std::runtime_error("Error on mic pipe");
				if (pfd.revents & POLLOUT)
				{
					auto packet = mic_buffer.pop();
					auto & buffer = packet.payload;

					int written = write(mic_pipe.get_fd(), buffer.data(), buffer.size());
					if (written < 0)
						throw std::system_error(errno, std::system_category());

					// Discard anything that didn't fit the buffer
				}
			}
		}
		catch (const std::exception & e)
		{
			U_LOG_E("Error in audio thread: %s", e.what());
		}
	}

	void process_mic_data(wivrn::audio_data && mic_data) override
	{
		mic_buffer.push(std::move(mic_data));
	}

	pulse_device(
	        const std::string & source_name,
	        const std::string & source_description,
	        const std::string & sink_name,
	        const std::string & sink_description,
	        const wivrn::from_headset::headset_info_packet & info,
	        wivrn::wivrn_session & session) :
	        session(session)
	{
		pa_connection cnx("WiVRn");

		if (info.microphone)
		{
			microphone = ensure_source(cnx, source_name.c_str(), source_description, info.microphone->num_channels, info.microphone->sample_rate);
			desc.microphone = {
			        .num_channels = info.microphone->num_channels,
			        .sample_rate = info.microphone->sample_rate};

			mic_pipe = open(microphone->socket.c_str(), O_WRONLY | O_NONBLOCK);
			if (not mic_pipe)
				throw std::system_error(errno, std::system_category(), "failed to open mic pipe " + microphone->socket.string());
			mic_thread = std::thread([this]() { run_mic(); });
			session.set_enabled(to_headset::tracking_control::id::microphone, true);
		}

		if (info.speaker)
		{
			speaker = ensure_sink(cnx, sink_name.c_str(), sink_description, info.speaker->num_channels, info.speaker->sample_rate);
			desc.speaker = {
			        .num_channels = info.speaker->num_channels,
			        .sample_rate = info.speaker->sample_rate};

			speaker_pipe = open(speaker->socket.c_str(), O_RDONLY | O_NONBLOCK);
			if (not speaker_pipe)
				throw std::system_error(errno, std::system_category(), "failed to open speaker pipe " + speaker->socket.string());

			speaker_thread = std::thread([this]() { run_speaker(); });
		}
	}
};

std::unique_ptr<audio_device> create_pulse_handle(
        const std::string & source_name,
        const std::string & source_description,
        const std::string & sink_name,
        const std::string & sink_description,
        const wivrn::from_headset::headset_info_packet & info,
        wivrn_session & session)
{
	try
	{
		return std::make_unique<pulse_device>(
		        source_name, source_description, sink_name, sink_description, info, session);
	}
	catch (std::exception & e)
	{
		U_LOG_I("Pulseaudio backend creation failed: %s", e.what());
		return nullptr;
	}
}

void unload_module(uintptr_t id)
{
	pa_connection cnx("WiVRn");
	unload_module(cnx, id);
}
} // namespace wivrn
