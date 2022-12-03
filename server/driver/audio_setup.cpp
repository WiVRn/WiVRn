#include "audio_setup.h"

#include <pulse/context.h>
#include <pulse/ext-device-manager.h>
#include <pulse/introspect.h>
#include <pulse/proplist.h>
#include <pulse/thread-mainloop.h>

#include <atomic>
#include <future>
#include <iomanip>
#include <iostream>
#include <optional>
#include <vector>

static const char * source_name = "WiVRn-mic";
static const char * sink_name = "WiVRn";

struct module_entry
{
	uint32_t module;
	uint32_t device;
	uint32_t monitor;
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
		        result = {.module = i->owner_module, .device = i->index, .monitor = i->monitor_source};
	        });
	auto op = pa_context_get_sink_info_by_name(ctx, name, cb, cb);
	pa_operation_unref(op);
	p.get_future().wait();
	return result;
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
	std::stringstream params;
	params << "sink_name=" << std::quoted(name)
	       << " channels=" << channels
	       << " rate=" << sample_rate
	       << " sink_properties=" << PA_PROP_DEVICE_DESCRIPTION << "=" << std::quoted(description)
	       << PA_PROP_DEVICE_ICON_NAME << "=network-wireless";
	;
	auto op = pa_context_load_module(ctx, "module-null-sink", params.str().c_str(), cb, cb);
	pa_operation_unref(op);
	module_index.get_future().wait();

	sink = get_sink(ctx, name);
	if (not sink)
		std::runtime_error("failed to create audio sink " +
		                   std::string(name));
	return *sink;
}

std::optional<uint32_t> get_simple_tcp(pa_context * ctx,
                                       int port)
{
	std::optional<uint32_t> result;
	std::promise<void> p;

	wrap_lambda cb([&result, &p, port_str = "port=" + std::to_string(port)](
	                       pa_context *, const pa_module_info * i, int eol) {
		if (eol)
		{
			p.set_value();
			return;
		}

		if (i->name == std::string("module-simple-protocol-tcp") and
		    i->argument and std::string_view(i->argument).find(port_str) != std::string_view::npos)
			result = i->index;
	});
	auto op = pa_context_get_module_info_list(ctx, cb, cb);
	pa_operation_unref(op);
	p.get_future().wait();
	return result;
}

uint32_t load_module_simple(pa_context * ctx, uint16_t port, std::optional<module_entry> microphone, std::optional<module_entry> speaker)
{
	std::stringstream params;
	params << "port=" << port;

	if (microphone)
		params << " playback=true sink=" << microphone->device;
	else
		params << " playback=false";

	if (speaker)
		params << " record=true source=" << speaker->monitor;
	else
		params << " record=false";

	std::string params_str = params.str();
	auto simple_tcp = get_simple_tcp(ctx, port);
	if (simple_tcp)
		unload_module(ctx, *simple_tcp);

	std::promise<uint32_t> module_index;
	wrap_lambda cb = [&module_index, &params_str](pa_context *,
	                                              uint32_t index) {
		if (index == uint32_t(-1))
			module_index.set_exception(
			        std::make_exception_ptr(std::runtime_error(
			                "failed to load module-simple-protocol-tcp "
			                "with parameters " +
			                params_str)));
		else
			module_index.set_value(index);
	};
	auto op = pa_context_load_module(ctx, "module-simple-protocol-tcp", params_str.c_str(), cb, cb);
	pa_operation_unref(op);
	return module_index.get_future().get();
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
	std::vector<uint32_t> modules;
	xrt::drivers::wivrn::to_headset::audio_stream_description desc;

	~pulse_publish_handle()
	{
		if (modules.empty())
			return;
		try
		{
			pa_connection cnx("WiVRn");
			for (const auto id: modules)
			{
				unload_module(cnx, id);
			}
		}
		catch (const std::exception & e)
		{
			std::cout << "failed to depublish pulseaudio modules: "
			          << e.what() << std::endl;
		}
	}

	xrt::drivers::wivrn::to_headset::audio_stream_description description() const
	{
		return desc;
	};

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

		std::optional<module_entry> source, sink;

		if (info.microphone)
		{
			source = ensure_sink(cnx, source_name.c_str(), source_description, info.microphone->num_channels, info.microphone->sample_rate);
			result->modules.push_back(source->module);
			result->desc.microphone = {.num_channels = info.microphone->num_channels, .sample_rate = info.microphone->sample_rate};
		}
		if (info.speaker)
		{
			sink = ensure_sink(cnx, sink_name.c_str(), sink_description, info.speaker->num_channels, info.speaker->sample_rate);
			result->modules.push_back(sink->module);
			result->desc.speaker = {.num_channels = info.speaker->num_channels, .sample_rate = info.speaker->sample_rate};
		}
		uint32_t simple_tcp =
		        load_module_simple(cnx, listen_port, source, sink);
		result->modules.push_back(simple_tcp);
		result->desc.port = listen_port;

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
