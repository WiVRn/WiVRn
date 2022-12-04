#include "audio_setup.h"

#include "util/u_logging.h"

#include "wivrn_sockets.h"
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
#include <sys/poll.h>
#include <thread>
#include <vector>

static const char * source_name = "WiVRn-mic";
static const char * sink_name = "WiVRn";
static const char * source_pipe = "wivrn-source";
static const char * sink_pipe = "wivrn-sink";

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
			xrt::drivers::wivrn::fd_base speaker_pipe;
			xrt::drivers::wivrn::fd_base mic_pipe;
			if (speaker)
			{
				speaker_pipe = open(speaker->socket.c_str(), O_RDONLY | O_NONBLOCK);
				if (speaker_pipe.get_fd() < 0)
					throw std::system_error(errno, std::system_category(), "failed to open speaker pipe " + speaker->socket.string());
				pfd.push_back({.fd = speaker_pipe.get_fd(), .events = POLLIN});
				pfd.push_back({.fd = client.get_fd(), .events = POLLOUT});
				// flush pipe, so we don't have old samples
				char sewer[1024];
				while (read(speaker_pipe.get_fd(), sewer, sizeof(sewer)) > 0)
				{}
			}
			if (microphone)
			{
				mic_pipe = open(microphone->socket.c_str(), O_WRONLY);
				if (mic_pipe.get_fd() < 0)
					throw std::system_error(errno, std::system_category(), "failed to open microphone pipe " + microphone->socket.string());
				pfd.push_back({.fd = client.get_fd(), .events = POLLIN});
				pfd.push_back({.fd = mic_pipe.get_fd(), .events = POLLOUT});
			}
			while (not quit)
			{
				int r = ::poll(pfd.data(), pfd.size(), 100);
				if (r < 0)
					throw std::system_error(errno, std::system_category());

				for (size_t i = 0; i < pfd.size(); ++i)
				{
					if (pfd[i].revents & (POLLHUP | POLLERR))
						throw std::runtime_error("Error on audio socket");

					if (pfd[i].revents & (POLLIN | POLLOUT))
					{
						// mark fd as ready
						pfd[i].fd = -pfd[i].fd;
						size_t base = i - i % 2;

						// if both in and out fd are ready, do something
						if (pfd[base].fd < 0 and pfd[base + 1].fd < 0)
						{
							pfd[base].fd = -pfd[base].fd;
							pfd[base + 1].fd = -pfd[base + 1].fd;

							ssize_t written = splice(pfd[base].fd, nullptr, pfd[base + 1].fd, nullptr, 1024, SPLICE_F_MOVE);
							if (written < 0)
								throw std::system_error(errno, std::system_category(), "failed to transfer audio data");
						}
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
			result->desc.microphone = {.num_channels = info.microphone->num_channels, .sample_rate = info.microphone->sample_rate};
		}
		if (info.speaker)
		{
			result->speaker = ensure_sink(cnx, sink_name.c_str(), sink_description, info.speaker->num_channels, info.speaker->sample_rate);
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
