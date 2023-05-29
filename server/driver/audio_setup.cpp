#include "audio_setup.h"

#include "util/u_logging.h"

#include "../wivrn_ipc.h"
#include "wivrn_sockets.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <pulse/context.h>
#include <pulse/ext-device-manager.h>
#include <pulse/introspect.h>
#include <pulse/proplist.h>
#include <pulse/thread-mainloop.h>

#include <atomic>
#include <filesystem>
#include <future>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/types.h>
#include <thread>
#include <vector>

static const char * source_name = "WiVRn-mic";
static const char * sink_name = "WiVRn";
static const char * source_pipe = "wivrn-source";
static const char * sink_pipe = "wivrn-sink";

static unsigned int buffer_size_ms = 10;

// max allowed bytes in the output pipe before we start discarding data
// total buffer size = buffer_size_ms + buffer_size_ms * buffer_size_mult
static unsigned int buffer_size_mult = 4;

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

template <typename... Args>
struct add_void;

template <typename T, typename... Args>
struct add_void<std::function<T(Args...)>>
{
	template <typename L>
	static T fn(Args... a, void * userdata)
	{
		L * f = (L *)userdata;
		return (*f)(a...);
	}
};

template <typename T>
class wrap_lambda
{
	T impl;

public:
	wrap_lambda(T && l) :
	        impl(l) {}

	operator auto()
	{
		using F = decltype(std::function(impl));
		return add_void<F>::template fn<T>;
	}

	operator void *()
	{
		return this;
	}
};

void unload_module(uintptr_t id);

void unload_module(pa_context * ctx, uint32_t id)
{
	std::promise<void> p;
	wrap_lambda cb = [&p, id](pa_context *, int success) {
		if (not success)
			std::cout << "failed to unload pulseaudio module " << id
			          << std::endl;
		else
			std::cout << "pulseaudio module " << id << " unloaded"
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
		std::runtime_error("failed to create audio sink " + std::string(name));
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
	       << " use_system_clock_for_timing=yes"
	       << " source_properties=" << PA_PROP_DEVICE_DESCRIPTION << "=" << std::quoted(description)
	       << PA_PROP_DEVICE_ICON_NAME << "=network-wireless";
	;
	auto op = pa_context_load_module(ctx, "module-pipe-source", params.str().c_str(), cb, cb);
	pa_operation_unref(op);
	module_index.get_future().wait();

	source = get_source(ctx, name);
	if (not source)
		std::runtime_error("failed to create audio source " + std::string(name));
	source->socket = fifo;
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

struct pulse_publish_handle : public audio_publish_handle
{
	xrt::drivers::wivrn::to_headset::audio_stream_description desc;

	std::thread net_thread;
	std::atomic<bool> quit;
	xrt::drivers::wivrn::TCPListener listener;

	std::optional<module_entry> speaker;
	std::optional<module_entry> microphone;

	int mic_buf_size;
	int spk_buf_size;

	~pulse_publish_handle()
	{
		quit = true;
		if (net_thread.joinable())
			net_thread.join();
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
				std::cout << "failed to depublish pulseaudio modules: "
				          << e.what() << std::endl;
			}
		}
	}

	xrt::drivers::wivrn::to_headset::audio_stream_description description() const
	{
		return desc;
	};

	void serve_client(xrt::drivers::wivrn::fd_base & client)
	{
		int nodelay = 1;
		if (setsockopt(client.get_fd(), IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0)
			U_LOG_W("failed to set TCP_NODELAY opeion on audio socket: %s", strerror(errno));
		try
		{
			// file descriptors:
			// in pairs (read, write)
			// negative if already available
			std::vector<pollfd> pfd;
			size_t num_pairs;

			xrt::drivers::wivrn::fd_base speaker_pipe;
			xrt::drivers::wivrn::fd_base mic_pipe;

			ssize_t bufsize[] = {0, 0};
			ssize_t max_bytes_in_pipe[] = {0, 0};
			char * buf = new char[std::max(spk_buf_size, mic_buf_size)];

			if (speaker)
			{
				bufsize[num_pairs] = spk_buf_size;
				speaker_pipe = open(speaker->socket.c_str(), O_RDONLY | O_NONBLOCK);
				if (speaker_pipe.get_fd() < 0)
					throw std::system_error(errno, std::system_category(), "failed to open speaker pipe " + speaker->socket.string());
				pfd.push_back({.fd = speaker_pipe.get_fd(), .events = POLLIN});
				pfd.push_back({.fd = client.get_fd(), .events = POLLOUT});

				num_pairs++;

				// flush pipe, so we don't have old samples
				while (read(speaker_pipe.get_fd(), buf, spk_buf_size) > 0)
				{}
			}
			if (microphone)
			{
				bufsize[num_pairs] = mic_buf_size;
				max_bytes_in_pipe[num_pairs] = mic_buf_size * buffer_size_mult;

				mic_pipe = open(microphone->socket.c_str(), O_WRONLY);
				if (mic_pipe.get_fd() < 0)
					throw std::system_error(errno, std::system_category(), "failed to open microphone pipe " + microphone->socket.string());
				pfd.push_back({.fd = client.get_fd(), .events = POLLIN});
				pfd.push_back({.fd = mic_pipe.get_fd(), .events = POLLOUT});

				num_pairs++;
			}
			while (not quit)
			{
				int r = ::poll(pfd.data(), pfd.size(), 100);
				if (r < 0)
					throw std::system_error(errno, std::system_category());

				for (size_t p = 0; p < num_pairs; p++)
				{
					int i = p * 2;
					int o = i + 1;
					if ((pfd[i].revents | pfd[o].revents) & (POLLHUP | POLLERR))
						throw std::runtime_error("Error on audio socket");

					if ((pfd[i].revents) & POLLIN)
					{
						ssize_t bytes_read = read(pfd[i].fd, buf, bufsize[p]);
						ssize_t bytes_to_write;

						if (max_bytes_in_pipe[p] != 0)
						{
							ssize_t bytes_in_pipe;
							if (ioctl(pfd[o].fd, FIONREAD, &bytes_in_pipe) == 0)
								bytes_to_write = std::min((int)max_bytes_in_pipe[p] - (int)bytes_in_pipe, (int)bytes_read);
							else
								bytes_to_write = bytes_read;
						}
						else
							bytes_to_write = bytes_read;

						ssize_t written = write(pfd[o].fd, buf, bytes_to_write);
						if (written < 0)
							throw std::system_error(errno, std::system_category(), "failed to transfer audio data");
					}
				}
			}
		}
		catch (const std::exception & e)
		{
			U_LOG_E("Error while serving audio: %s", e.what());
		}
	}

	void run()
	{
		pthread_setname_np(pthread_self(), "audio_thread");

		try
		{
			while (not quit)
			{
				pollfd pfd{};
				pfd.fd = listener.get_fd();
				pfd.events = POLL_IN;

				int r = poll(&pfd, 1, 100);
				if (r < 0)
					throw std::system_error(errno, std::system_category());
				if (pfd.revents & (POLLHUP | POLLERR))
					throw std::runtime_error("Error on audio socket");
				if (pfd.revents & POLLIN)
				{
					auto client = listener.accept<xrt::drivers::wivrn::fd_base>().first;
					serve_client(client);
				}
			}
		}
		catch (const std::exception & e)
		{
			U_LOG_E("Error in audio thread: %s", e.what());
		}
	}

	static std::shared_ptr<pulse_publish_handle> create(
	        const std::string & source_name,
	        const std::string & source_description,
	        const std::string & sink_name,
	        const std::string & sink_description,
	        const uint16_t listen_port,
	        const xrt::drivers::wivrn::from_headset::headset_info_packet & info)
	{
		pa_connection cnx("WiVRn");
		auto result = std::make_shared<pulse_publish_handle>();

		if (info.microphone)
		{
			result->microphone = ensure_source(cnx, source_name.c_str(), source_description, info.microphone->num_channels, info.microphone->sample_rate);
			result->mic_buf_size = 2 * info.microphone->num_channels * (info.microphone->sample_rate / 1000 * buffer_size_ms);
			result->desc.microphone = {.num_channels = info.microphone->num_channels, .sample_rate = info.microphone->sample_rate};
		}
		if (info.speaker)
		{
			result->speaker = ensure_sink(cnx, sink_name.c_str(), sink_description, info.speaker->num_channels, info.speaker->sample_rate);
			result->spk_buf_size = 2 * info.speaker->num_channels * (info.speaker->sample_rate / 1000 * buffer_size_ms);
			result->desc.speaker = {.num_channels = info.speaker->num_channels, .sample_rate = info.speaker->sample_rate};
		}

		result->listener = xrt::drivers::wivrn::TCPListener(listen_port);
		result->desc.port = listen_port;
		result->net_thread = std::thread(&pulse_publish_handle::run, result.get());

		return result;
	}
};

std::shared_ptr<audio_publish_handle> audio_publish_handle::create(
        const std::string & source_name,
        const std::string & source_description,
        const std::string & sink_name,
        const std::string & sink_description,
        const uint16_t listen_port,
        const xrt::drivers::wivrn::from_headset::headset_info_packet & info)
{
	return pulse_publish_handle::create(source_name, source_description, sink_name, sink_description, listen_port, info);
}

void unload_module(uintptr_t id)
{
	pa_connection cnx("WiVRn");
	unload_module(cnx, id);
}
