// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main file for WiVRn Monado service.
 */

#include "application.h"
#include "openxr/openxr.h"
#include "sleep_inhibitor.h"
#include "util/u_trace_marker.h"

#include "active_runtime.h"
#include "avahi_publisher.h"
#include "driver/configuration.h"
#include "driver/wivrn_connection.h"
#include "exit_codes.h"
#include "hostname.h"
#include "ipc_server_cb.h"
#include "protocol_version.h"
#include "start_application.h"
#include "utils/overloaded.h"
#include "version.h"
#include "wivrn_config.h"
#include "wivrn_ipc.h"
#include "wivrn_packets.h"
#include "wivrn_sockets.h"

#include "wivrn_server_dbus.h"

#include <CLI/CLI.hpp>
#include <avahi-glib/glib-watch.h>
#include <chrono> // IWYU pragma: keep
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <libnotify/notification.h>
#include <memory>
#include <poll.h>
#include <random>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>
#include <libnotify/notify.h>

#include <shared/ipc_protocol.h>
#include <util/u_file.h>

#if WIVRN_USE_SYSTEMD
#include "start_systemd_unit.h"
#endif

// Insert the on load constructor to init trace marker.
U_TRACE_TARGET_SETUP(U_TRACE_WHICH_SERVICE)

extern "C"
{
	int listen_socket = -1;
}

using namespace wivrn;
using namespace std::chrono_literals;

static std::unique_ptr<TCPListener> listener;
std::optional<wivrn::typed_socket<wivrn::UnixDatagram, from_monado::packets, to_monado::packets>> wivrn_ipc_socket_main_loop;
std::optional<wivrn::typed_socket<wivrn::UnixDatagram, to_monado::packets, from_monado::packets>> wivrn_ipc_socket_monado;

std::filesystem::path socket_path()
{
	char sock_file[PATH_MAX];
	size_t size = u_file_get_path_in_runtime_dir(XRT_IPC_MSG_SOCK_FILENAME, sock_file, PATH_MAX);

	return {sock_file, sock_file + size};
}

int create_listen_socket()
{
	sockaddr_un addr{};

	auto sock_file = socket_path();

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sock_file.c_str());

	int fd = socket(PF_UNIX, SOCK_STREAM, 0);
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	int ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));

	// no other instance is running, or we would have never arrived here
	if (ret < 0 && errno == EADDRINUSE)
	{
		std::cerr << "Removing stale socket file " << sock_file << std::endl;

		ret = unlink(sock_file.c_str());
		if (ret < 0)
		{
			std::cerr << "Failed to remove stale socket file " << sock_file << ": " << strerror(errno) << std::endl;
			throw std::system_error(errno, std::system_category());
		}
		ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
	}

	if (ret < 0)
	{
		std::cerr << "Could not bind socket to path " << sock_file << ": " << strerror(errno) << ". Is the service running already?" << std::endl;
		if (errno == EADDRINUSE)
		{
			std::cerr << "If WiVRn is not running, delete " << sock_file << " before starting a new instance" << std::endl;
		}
		close(fd);
		throw std::system_error(errno, std::system_category());
	}

	ret = listen(fd, IPC_MAX_CLIENTS);
	if (ret < 0)
	{
		close(fd);
		throw std::system_error(errno, std::system_category());
	}

	return fd;
}

static bool pressure_vessel_openxr_support()
{
	auto pv_var = std::getenv("PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES");
	return pv_var and pv_var == std::string_view("1");
}

static void append_delim(std::string & to, std::string_view what, char delim)
{
	if (not to.empty())
		to += delim;
	to += what;
}

// search for the directory in path named needle
// with d = /a/b/c/d/e and needle = c
// return /a/b/c
// if it can't be found, return the full path
static std::filesystem::path find_dir(const std::filesystem::path & d, const std::filesystem::path & needle)
{
	for (auto copy = d; copy != copy.parent_path(); copy = copy.parent_path())
	{
		if (copy.filename() == needle)
			return copy;
	}
	return d;
}

static std::string steam_command()
{
	std::string command;

	if (not pressure_vessel_openxr_support())
		command = "PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES=1";

	if (auto p = active_runtime::openvr_compat_path().string(); not p.empty())
	{
		// /usr cannot be shared in pressure vessel container
		if (p.starts_with("/usr"))
			append_delim(command, " VR_OVERRIDE=/run/host" + p, ' ');
		else if (p.starts_with("/var"))
			append_delim(command, "PRESSURE_VESSEL_FILESYSTEMS_RW=" + find_dir(p, "io.github.wivrn.wivrn").string(), ' ');
	}

	if (not command.empty())
		command += " %command%";

	return command;
}

namespace
{
int control_pipe_fds[2];

GMainLoop * main_loop;
const AvahiPoll * poll_api;

guint server_watch;
guint server_kill_watch;
pid_t server_pid;
std::optional<std::jthread> connection_thread;

std::unique_ptr<children_manager> children;

bool quitting_main_loop;
bool do_fork;
bool do_active_runtime;
wivrn::service_publication publication;

guint listener_watch;

wivrn_connection::encryption_state enc_state = wivrn_connection::encryption_state::enabled;
guint pairing_timeout;
std::string pin;
NotifyNotification * pin_notification;

// Delay until the next connection is allowed, increased after each incorrect attempt
const std::chrono::milliseconds default_delay_next_try = 10ms;
std::chrono::milliseconds delay_next_try = default_delay_next_try;

WivrnServer * dbus_server;

/* TODO: Document FSM
 */
std::optional<active_runtime> runtime_setter;
std::optional<avahi_publisher> publisher;
std::optional<sleep_inhibitor> inhibitor;

gboolean headset_connected(gint fd, GIOCondition condition, gpointer user_data);
void stop_listening();
void on_headset_info_packet(const wivrn::from_headset::headset_info_packet & info);
void expose_known_keys_on_dbus();
void set_encryption_state(wivrn_connection::encryption_state new_enc_state);

void start_publishing();
void stop_publishing();

void update_fsm();

void start_server(configuration config)
{
	server_pid = do_fork ? fork() : 0;

	if (server_pid < 0)
	{
		perror("fork");
		g_main_loop_quit(main_loop);
	}
	else if (server_pid == 0)
	{
		// foveation code does not allow oversampling
		setenv("XRT_COMPOSITOR_SCALE_PERCENTAGE", "100", true);

		setenv("XRT_COMPOSITOR_COMPUTE", "1", true);

		setenv("AMD_DEBUG", "lowlatencyenc", false);

		wivrn::ipc_server_cb server_cb;

		ipc_server_main_info server_info{
		        .udgci = {
		                .window_title = "WiVRn",
#if WIVRN_FEATURE_DEBUG_GUI
		                .open = config.debug_gui ? U_DEBUG_GUI_OPEN_ALWAYS : U_DEBUG_GUI_OPEN_AUTO,
#else
		                .open = U_DEBUG_GUI_OPEN_NEVER,
#endif
		        },
		        .no_stdin = true,
		};

		try
		{
			exit(ipc_server_main_common(&server_info, &server_cb, nullptr));
		}
		catch (std::exception & e)
		{
			std::cerr << e.what() << std::endl;
			exit(EXIT_FAILURE);
		}

		__builtin_unreachable();
	}
	else
	{
		std::cerr << "Server started, PID " << server_pid << std::endl;

		assert(server_watch == 0);
		assert(server_kill_watch == 0);
		wivrn_server_set_session_running(dbus_server, true);
		server_watch = g_child_watch_add(server_pid, [](pid_t, int status, void *) {
			wivrn_server_set_session_running(dbus_server, false);
			display_child_status(status, "Server");
			g_source_remove(server_watch);
			if (server_kill_watch)
				g_source_remove(server_kill_watch);

			server_watch = 0;
			server_kill_watch = 0;

			update_fsm(); }, nullptr);
	}

	if (do_active_runtime)
		runtime_setter.emplace();
}

void kill_server()
{
	wivrn_ipc_socket_main_loop->send(to_monado::stop{});

	// Send SIGTERM after 1s if it is still running
	server_kill_watch = g_timeout_add(1000, [](void *) {
		assert(server_pid > 0);
		kill(-server_pid, SIGTERM);
		return G_SOURCE_REMOVE; }, 0);
}

void start_listening()
{
	if (listener)
		return;

	assert(listener_watch == 0);

	listener = std::make_unique<TCPListener>(wivrn::default_port);
	auto source_listener = g_unix_fd_source_new(listener->get_fd(), GIOCondition::G_IO_IN);
	g_source_set_callback(source_listener, G_SOURCE_FUNC(&headset_connected), nullptr, nullptr);
	listener_watch = g_source_attach(source_listener, nullptr);
	g_source_unref(source_listener);
}

void stop_listening()
{
	if (!listener)
		return;

	assert(listener_watch != 0);

	g_source_remove(listener_watch);
	listener_watch = 0;
	listener.reset();
}

void start_publishing()
{
	switch (publication)
	{
		case wivrn::service_publication::none:
			return;

		case wivrn::service_publication::avahi: {
			if (publisher)
				return;

			char protocol_string[17];
			sprintf(protocol_string, "%016" PRIx64, wivrn::protocol_version);
			std::map<std::string, std::string> TXT = {
			        {"protocol", protocol_string},
			        {"version", wivrn::display_version()},
			        {"cookie", server_cookie()},
			};
			publisher.emplace(poll_api, hostname(), "_wivrn._tcp", wivrn::default_port, TXT);
		}
	}
}

void stop_publishing()
{
	publisher.reset();
}

void update_fsm()
{
	bool server_running = server_watch != 0 or connection_thread;
	bool app_running = children->running();

	if (quitting_main_loop)
	{
		connection_thread.reset();

		if (server_running)
			kill_server();

		if (app_running)
			children->stop();

		if (not server_running and not app_running)
		{
			children.reset(nullptr);
			g_main_loop_quit(main_loop);
		}
	}
	else
	{
		if (not server_running and app_running)
			children->stop();

		if (not server_running)
		{
			runtime_setter.reset();

			g_timeout_add(delay_next_try.count(), [](void *) {
				start_listening();
				start_publishing();
				wivrn_server_set_headset_connected(dbus_server, false);
				return G_SOURCE_REMOVE; }, 0);
		}
	}
}

int sigint(void * sig_nr)
{
	std::cerr << strsignal((uintptr_t)sig_nr) << std::endl;

	quitting_main_loop = true;

	update_fsm();

	return G_SOURCE_CONTINUE;
}

gboolean headset_connected_success(void *)
{
	assert(connection_thread);
	connection_thread.reset();

	if (enc_state == wivrn_connection::encryption_state::pairing)
		set_encryption_state(wivrn_connection::encryption_state::enabled);

	init_cleanup_functions();

	std::cerr << "Client connected" << std::endl;

	expose_known_keys_on_dbus();

	configuration c;
	start_server(c);
	try
	{
		children->start_application(c.application);
	}
	catch (std::exception & e)
	{
		std::cerr << "Failed to start application: " << e.what() << std::endl;
	}

	delay_next_try = default_delay_next_try;

	connection.reset();
	return G_SOURCE_REMOVE;
}

gboolean headset_connected_failed(void *)
{
	assert(connection_thread);
	connection_thread.reset();

	update_fsm();
	return G_SOURCE_REMOVE;
}

gboolean headset_connected_incorrect_pin(void *)
{
	assert(connection_thread);
	connection_thread.reset();

	delay_next_try = 2 * delay_next_try;
	std::cerr << "Waiting " << delay_next_try << " until the next attempt is allowed" << std::endl;

	update_fsm();
	return G_SOURCE_REMOVE;
}

gboolean headset_connected(gint fd, GIOCondition condition, gpointer user_data)
{
	assert(server_watch == 0);
	assert(not children->running());
	assert(not connection_thread);
	assert(listener);

	TCP tcp = listener->accept().first;
	stop_listening();
	stop_publishing();

	connection_thread.emplace([](std::stop_token stop_token, TCP && tcp, std::string pin, wivrn_connection::encryption_state enc_state) {
		try
		{
			connection = std::make_unique<wivrn_connection>(stop_token, enc_state, pin, std::move(tcp));
			g_main_context_invoke(nullptr, &headset_connected_success, nullptr);
		}
		catch (wivrn::incorrect_pin &)
		{
			std::cerr << "Incorrect PIN" << std::endl;
			g_main_context_invoke(nullptr, &headset_connected_incorrect_pin, nullptr);
		}
		catch (std::exception & e)
		{
			std::cerr << "Client connection failed: " << e.what() << std::endl;
			g_main_context_invoke(nullptr, &headset_connected_failed, nullptr);
		}
	},
	                          std::move(tcp),
	                          pin,
	                          enc_state);

	return G_SOURCE_CONTINUE;
}

gboolean control_received(gint fd, GIOCondition condition, gpointer user_data)
{
	auto packet = wivrn_ipc_socket_main_loop->receive();
	if (packet)
	{
		std::visit(utils::overloaded{
		                   [&](const wivrn::from_headset::headset_info_packet & info) {
			                   on_headset_info_packet(info);
			                   inhibitor.emplace();
			                   wivrn_server_set_headset_connected(dbus_server, true);
		                   },
		                   [&](const wivrn::from_headset::settings_changed & settings) {
			                   wivrn_server_set_preferred_refresh_rate(dbus_server, settings.preferred_refresh_rate);
			                   wivrn_server_set_bitrate(dbus_server, settings.bitrate_bps);
		                   },
		                   [&](const wivrn::from_headset::start_app & request) {
			                   const auto & apps = list_applications();
			                   if (auto it = apps.find(request.app_id); it != apps.end())
				                   children->start_application(it->second.exec);
		                   },
		                   [&](const from_monado::headset_connected &) {
			                   stop_publishing();
			                   inhibitor.emplace();
			                   wivrn_server_set_headset_connected(dbus_server, true);
		                   },
		                   [&](const from_monado::headset_disconnected &) {
			                   start_publishing();
			                   inhibitor.reset();
			                   wivrn_server_set_headset_connected(dbus_server, false);
		                   },
		                   [&](const from_monado::server_error & e) {
			                   wivrn_server_emit_server_error(dbus_server, e.where.c_str(), e.message.c_str());
		                   },
		           },
		           *packet);
	}

	return G_SOURCE_CONTINUE;
}

void set_encryption_state(wivrn_connection::encryption_state new_enc_state)
{
	// TODO translate the notifications
	if (pin_notification)
	{
		notify_notification_close(pin_notification, nullptr);
		g_object_unref(G_OBJECT(pin_notification));
		pin_notification = nullptr;
	}

	switch (new_enc_state)
	{
		case wivrn_connection::encryption_state::disabled:
			pin = "";
			std::cerr << "Encryption is disabled" << std::endl;
			wivrn_server_set_pairing_enabled(dbus_server, false);
			wivrn_server_set_encryption_enabled(dbus_server, false);
			break;

		case wivrn_connection::encryption_state::enabled:
			pin = "";
			if (enc_state != wivrn_connection::encryption_state::enabled)
				std::cerr << "Headset pairing is disabled" << std::endl;

			wivrn_server_set_pairing_enabled(dbus_server, false);
			wivrn_server_set_encryption_enabled(dbus_server, true);
			break;

		case wivrn_connection::encryption_state::pairing:
			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<> distrib(0, 999999);

			char buffer[7];
			snprintf(buffer, 7, "%06d", distrib(gen));
			pin = buffer;
			std::cerr << "To pair a new headset use PIN code: " << pin << std::endl;
			wivrn_server_set_pairing_enabled(dbus_server, true);
			wivrn_server_set_encryption_enabled(dbus_server, true);

			// Desktop notification
			pin_notification = notify_notification_new("PIN", pin.c_str(), "dialog-password");
			notify_notification_set_timeout(pin_notification, NOTIFY_EXPIRES_NEVER);
			// TODO: notify_notification_set_image_from_pixbuf
			notify_notification_show(pin_notification, nullptr);

			break;
	}

	enc_state = new_enc_state;
	wivrn_server_set_pin(dbus_server, pin.c_str());
}

gboolean on_handle_disconnect(WivrnServer * skeleton,
                              GDBusMethodInvocation * invocation,
                              gpointer user_data)
{
	wivrn_ipc_socket_main_loop->send(to_monado::disconnect{});

	g_dbus_method_invocation_return_value(invocation, nullptr);
	return G_SOURCE_CONTINUE;
}

gboolean on_handle_quit(WivrnServer * skeleton, GDBusMethodInvocation * invocation, gpointer user_data)
{
	quitting_main_loop = true;
	update_fsm();

	g_dbus_method_invocation_return_value(invocation, nullptr);
	return G_SOURCE_CONTINUE;
}

gboolean on_handle_revoke_key(WivrnServer * skeleton, GDBusMethodInvocation * invocation, gpointer user_data)
{
	GVariant * args = g_dbus_method_invocation_get_parameters(invocation);

	char * publickey;
	g_variant_get_child(args, 0, "s", &publickey);

	wivrn::remove_known_key(publickey);

	g_free(publickey);

	expose_known_keys_on_dbus();
	g_dbus_method_invocation_return_value(invocation, nullptr);
	return G_SOURCE_CONTINUE;
}

gboolean on_handle_rename_key(WivrnServer * skeleton, GDBusMethodInvocation * invocation, gpointer user_data)
{
	GVariant * args = g_dbus_method_invocation_get_parameters(invocation);

	char * publickey;
	char * name;
	g_variant_get_child(args, 0, "s", &publickey);
	g_variant_get_child(args, 1, "s", &name);

	wivrn::rename_known_key({.public_key = publickey, .name = name});

	g_free(publickey);
	g_free(name);

	expose_known_keys_on_dbus();
	g_dbus_method_invocation_return_value(invocation, nullptr);
	return G_SOURCE_CONTINUE;
}

gboolean on_handle_enable_pairing(WivrnServer * skeleton, GDBusMethodInvocation * invocation, gpointer user_data)
{
	set_encryption_state(wivrn_connection::encryption_state::pairing);

	if (pairing_timeout)
		g_source_remove(pairing_timeout);

	int timeout_secs;
	GVariant * args = g_dbus_method_invocation_get_parameters(invocation);
	g_variant_get_child(args, 0, "i", &timeout_secs);

	if (timeout_secs > 0)
	{
		pairing_timeout = g_timeout_add(timeout_secs * 1000, [](void *) {
			pairing_timeout = 0;
			set_encryption_state(wivrn_connection::encryption_state::enabled);
			return G_SOURCE_REMOVE; }, nullptr);
	}

	g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", pin.c_str()));
	return G_SOURCE_CONTINUE;
}

gboolean on_handle_disable_pairing(WivrnServer * skeleton, GDBusMethodInvocation * invocation, gpointer user_data)
{
	set_encryption_state(wivrn_connection::encryption_state::enabled);
	g_dbus_method_invocation_return_value(invocation, nullptr);
	return G_SOURCE_CONTINUE;
}

void on_bitrate(WivrnServer * server, const GParamSpec * pspec, gpointer data)
{
	auto bitrate = wivrn_server_get_bitrate(server);
	if (bitrate > 0)
		wivrn_ipc_socket_main_loop->send(to_monado::set_bitrate{bitrate});
}

void on_json_configuration(WivrnServer * server, const GParamSpec * pspec, gpointer data)
{
	const char * json = wivrn_server_get_json_configuration(server);

	std::filesystem::path config = configuration::get_config_file();
	std::filesystem::path config_new = config;
	config_new += ".new";

	{
		std::ofstream file(config_new);
		file.write(json, strlen(json));
	}

	std::error_code ec;
	std::filesystem::rename(config_new, config, ec);

	if (ec)
		std::cerr << "Failed to save configuration: " << ec.message() << std::endl;

	wivrn_server_set_steam_command(dbus_server, steam_command().c_str());
}

void expose_known_keys_on_dbus()
{
	GVariantBuilder * builder = g_variant_builder_new(G_VARIANT_TYPE("a(ssx)"));
	for (const auto & i: known_keys())
	{
		g_variant_builder_add(builder,
		                      "(ssx)",
		                      i.name.c_str(),
		                      i.public_key.c_str(),
		                      i.last_connection ? std::chrono::duration_cast<std::chrono::seconds>((*i.last_connection).time_since_epoch()).count() : 0);
	}
	GVariant * value = g_variant_new("a(ssx)", builder);
	g_variant_builder_unref(builder);
	wivrn_server_set_known_keys(dbus_server, value);
}

void on_headset_info_packet(const wivrn::from_headset::headset_info_packet & info)
{
	GVariantBuilder * builder;

	builder = g_variant_builder_new(G_VARIANT_TYPE("ad"));
	for (double rate: info.available_refresh_rates)
	{
		g_variant_builder_add(builder, "d", rate);
	}
	GVariant * value_refresh_rates = g_variant_new("ad", builder);
	g_variant_builder_unref(builder);
	wivrn_server_set_available_refresh_rates(dbus_server, value_refresh_rates);

	wivrn_server_set_preferred_refresh_rate(dbus_server, info.settings.preferred_refresh_rate);

	wivrn_server_set_bitrate(dbus_server, info.settings.bitrate_bps);

	auto speaker = info.speaker.value_or(wivrn::from_headset::headset_info_packet::audio_description{});
	wivrn_server_set_speaker_channels(dbus_server, speaker.num_channels);
	wivrn_server_set_speaker_sample_rate(dbus_server, speaker.sample_rate);

	auto mic = info.microphone.value_or(wivrn::from_headset::headset_info_packet::audio_description{});
	wivrn_server_set_mic_channels(dbus_server, mic.num_channels);
	wivrn_server_set_mic_sample_rate(dbus_server, mic.sample_rate);

	builder = g_variant_builder_new(G_VARIANT_TYPE("a(dddd)"));
	for (XrFovf fov: info.fov)
	{
		g_variant_builder_add(builder, "(dddd)", fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown);
	}
	GVariant * value_field_of_view = g_variant_new("a(dddd)", builder);
	g_variant_builder_unref(builder);
	wivrn_server_set_field_of_view(dbus_server, value_field_of_view);

	wivrn_server_set_hand_tracking(dbus_server, info.hand_tracking);
	wivrn_server_set_eye_gaze(dbus_server, info.eye_gaze);
	wivrn_server_set_face_tracking(dbus_server, info.face_tracking != wivrn::from_headset::face_type::none);

	std::vector<const char *> codecs;
	static const std::map<video_codec, const char *> codec_names = {
	        {h264, "h264"},
	        {h265, "h265"},
	        {av1, "av1"}};
	for (video_codec codec: info.supported_codecs)
	{
		auto it = codec_names.find(codec);
		if (it != codec_names.end())
			codecs.push_back(it->second);
	}
	codecs.push_back(nullptr);
	wivrn_server_set_supported_codecs(dbus_server, codecs.data());
	wivrn_server_set_system_name(dbus_server, info.system_name.c_str());
}

void on_name_acquired(GDBusConnection * connection, const gchar * name, gpointer user_data)
{
#if WIVRN_USE_SYSTEMD
	try
	{
		children = std::make_unique<systemd_units_manager>(connection, update_fsm);
	}
	catch (...)
#endif
	{
		children = std::make_unique<forked_children>(update_fsm);
	}

	dbus_server = wivrn_server_skeleton_new();

	g_signal_connect(dbus_server,
	                 "handle-disconnect",
	                 G_CALLBACK(on_handle_disconnect),
	                 NULL);

	g_signal_connect(dbus_server,
	                 "handle-quit",
	                 G_CALLBACK(on_handle_quit),
	                 NULL);

	g_signal_connect(dbus_server,
	                 "handle-revoke-key",
	                 G_CALLBACK(on_handle_revoke_key),
	                 NULL);

	g_signal_connect(dbus_server,
	                 "handle-rename-key",
	                 G_CALLBACK(on_handle_rename_key),
	                 NULL);

	if (enc_state != wivrn_connection::encryption_state::disabled)
	{
		g_signal_connect(dbus_server,
		                 "handle-enable-pairing",
		                 G_CALLBACK(on_handle_enable_pairing),
		                 NULL);

		g_signal_connect(dbus_server,
		                 "handle-disable-pairing",
		                 G_CALLBACK(on_handle_disable_pairing),
		                 NULL);
	}

	wivrn_server_set_steam_command(dbus_server, steam_command().c_str());

	on_headset_info_packet({});

	std::string config;
	try
	{
		config = configuration::read_configuration().dump();
	}
	catch (std::exception & e)
	{
		std::cerr << "Invalid configuration: " << e.what() << std::endl;
	}
	wivrn_server_set_json_configuration(dbus_server, config.c_str());

	expose_known_keys_on_dbus();

	g_signal_connect(dbus_server, "notify::json-configuration", G_CALLBACK(on_json_configuration), NULL);
	g_signal_connect(dbus_server, "notify::bitrate", G_CALLBACK(on_bitrate), NULL);

	g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(dbus_server),
	                                 connection,
	                                 "/io/github/wivrn/Server",
	                                 NULL);

	if (enc_state != wivrn_connection::encryption_state::disabled and known_keys().empty())
		set_encryption_state(wivrn_connection::encryption_state::pairing);
	else
		set_encryption_state(enc_state);
}

auto create_dbus_connection()
{
	// When process has cap_sys_nice, DBUS_SESSION_BUS_ADDRESS is ignored by gdbus
	GError * error = nullptr;
	if (const char * bus_address = getenv("DBUS_SESSION_BUS_ADDRESS"))
	{
		auto connection = g_dbus_connection_new_for_address_sync(
		        bus_address,
		        GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION | G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT),
		        nullptr,
		        nullptr,
		        &error);
		if (error)
		{
			auto msg = std::string("Failed to connect to dbus at ") + bus_address + ": " + error->message;
			g_error_free(error);
			throw std::runtime_error(msg);
		}
		return connection;
	}
	auto connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
	if (error)
	{
		auto msg = std::string("Failed to connect to session bus: ") + error->message;
		g_error_free(error);
		throw std::runtime_error(msg);
	}
	return connection;
}

} // namespace

int inner_main(int argc, char * argv[], bool show_instructions)
{
	std::cerr << "WiVRn " << wivrn::display_version() << " starting" << std::endl;
	if (show_instructions)
	{
		if (auto command = steam_command(); not command.empty())
			std::cerr << "For Steam games, set command to " << command << std::endl;
	}

	std::filesystem::create_directories(socket_path().parent_path());

	listen_socket = create_listen_socket();

	u_trace_marker_init();

	// Initialize main loop
	main_loop = g_main_loop_new(nullptr, false);
	auto main_context = g_main_loop_get_context(main_loop);

	// avahi glib integration
	AvahiGLibPoll * glib_poll = avahi_glib_poll_new(main_context, G_PRIORITY_DEFAULT);
	poll_api = avahi_glib_poll_get(glib_poll);

	// Create a socket to report monado status to the main loop
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, control_pipe_fds) < 0)
	{
		perror("socketpair");
		return wivrn_exit_code::cannot_create_socketpair;
	}
	fcntl(control_pipe_fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(control_pipe_fds[1], F_SETFD, FD_CLOEXEC);
	wivrn_ipc_socket_main_loop.emplace(control_pipe_fds[0]);
	wivrn_ipc_socket_monado.emplace(control_pipe_fds[1]);

	auto control_listener = g_unix_fd_source_new(control_pipe_fds[0], GIOCondition::G_IO_IN);
	g_source_set_callback(control_listener, G_SOURCE_FUNC(&control_received), nullptr, nullptr);
	g_source_attach(control_listener, nullptr);
	g_source_unref(control_listener);

	// Initialize avahi publisher
	start_publishing();

	// Initialize listener
	start_listening();

	// Catch SIGINT & SIGTERM
	g_unix_signal_add(SIGINT, &sigint, (void *)SIGINT);
	g_unix_signal_add(SIGTERM, &sigint, (void *)SIGTERM);

	// Add dbus server
	auto connection = create_dbus_connection();
	g_bus_own_name_on_connection(connection,
	                             "io.github.wivrn.Server",
	                             G_BUS_NAME_OWNER_FLAGS_NONE,
	                             on_name_acquired,
	                             nullptr,
	                             nullptr,
	                             nullptr);

	// Initialize libnotify
	notify_init("WiVRn");

	// Main loop
	g_main_loop_run(main_loop);

	// Cleanup
	notify_uninit();
	runtime_setter.reset();
	stop_publishing();
	stop_listening();
	avahi_glib_poll_free(glib_poll);
	g_main_loop_unref(main_loop);

#if WIVRN_USE_SYSTEMD
	std::error_code ec;
	std::filesystem::remove(socket_path(), ec);
#endif

	return wivrn_exit_code::success;
}

int main(int argc, char * argv[])
{
	CLI::App app;

	std::string config_file;
	app.add_option("-f", config_file, "configuration file")->option_text("FILE")->check(CLI::ExistingFile);
	auto version_flag = app.add_flag("--version")->description("print version and exit");
	auto no_active_runtime = app.add_flag("--no-manage-active-runtime")->description("don't set the active runtime on connection");
	auto early_active_runtime = app.add_flag("--early-active-runtime")->description("forcibly manages the active runtime even if no headset present");
	auto no_instructions = app.add_flag("--no-instructions")->group("");
	auto no_fork = app.add_flag("--no-fork")->description("disable fork to serve connection")->group("Debug");
	auto no_publish = app.add_flag("--no-publish-service")->description("disable publishing the service through avahi");
	auto no_encrypt = app.add_flag("--no-encrypt")->description("disable encryption")->group("Debug");

	CLI11_PARSE(app, argc, argv);

	if (*version_flag)
	{
		std::cout << "WiVRn version " << wivrn::display_version() << std::endl;
		return 0;
	}

	if (not config_file.empty())
		configuration::set_config_file(config_file);

	if (*early_active_runtime)
	{
		do_active_runtime = false;
		runtime_setter.emplace();
	}
	else
		do_active_runtime = not *no_active_runtime;

	do_fork = not *no_fork;
	if (*no_encrypt)
		enc_state = wivrn_connection::encryption_state::disabled;

	if (*no_publish)
		publication = wivrn::service_publication::none;
	else
		publication = configuration().publication;

	try
	{
		return inner_main(argc, argv, not *no_instructions);
	}
	catch (std::system_error & e)
	{
		std::cerr << e.what() << std::endl;
		if (e.code().category() == avahi_error_category())
			return wivrn_exit_code::cannot_connect_to_avahi;
	}
	catch (std::exception & e)
	{
		std::cerr << e.what() << std::endl;
		return wivrn_exit_code::unknown_error;
	}
}
