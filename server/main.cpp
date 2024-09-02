// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main file for WiVRn Monado service.
 */

#include "util/u_trace_marker.h"

#include "active_runtime.h"
#include "avahi_publisher.h"
#include "driver/configuration.h"
#include "hostname.h"
#include "start_application.h"
#include "version.h"
#include "wivrn_config.h"
#include "wivrn_ipc.h"
#include "wivrn_sockets.h"

#include <CLI/CLI.hpp>
#include <avahi-glib/glib-watch.h>
#include <filesystem>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gio/gio.h>
#include <glib-unix.h>
#include <glib.h>

#include <shared/ipc_protocol.h>
#include <util/u_file.h>

// Insert the on load constructor to init trace marker.
U_TRACE_TARGET_SETUP(U_TRACE_WHICH_SERVICE)

extern "C"
{
	int
	ipc_server_main(int argc, char * argv[]);
}

using namespace xrt::drivers::wivrn;

std::unique_ptr<TCP> tcp;
std::unique_ptr<TCPListener> listener;
int control_pipe_fd;

#ifdef WIVRN_USE_SYSTEMD
std::string socket_path()
{
	char sock_file[PATH_MAX];
	size_t size = u_file_get_path_in_runtime_dir(XRT_IPC_MSG_SOCK_FILENAME, sock_file, PATH_MAX);

	return {sock_file, size};
}

int create_listen_socket()
{
	sockaddr_un addr{};

	auto sock_file = socket_path();

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, sock_file.c_str());

	int fd = socket(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
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

	// we use "socket activation" mode of monado,
	// it requires fd to be SD_LISTEN_FDS_START, which is 3
	assert(fd == 3);

	setenv("LISTEN_FDS", "1", true);

	return fd;
}
#endif

void display_child_status(int wstatus, const std::string & name)
{
	std::cerr << name << " exited, exit status " << WEXITSTATUS(wstatus);
	if (WIFSIGNALED(wstatus))
	{
		std::cerr << ", received signal " << sigabbrev_np(WTERMSIG(wstatus)) << " ("
		          << strsignal(WTERMSIG(wstatus)) << ")"
		          << (WCOREDUMP(wstatus) ? ", core dumped" : "") << std::endl;
	}
	else
	{
		std::cerr << std::endl;
	}
}

static std::filesystem::path flatpak_app_path()
{
	const std::string key("app-path=");
	std::string line;
	std::ifstream info("/.flatpak-info");
	while (std::getline(info, line))
	{
		if (line.starts_with(key))
		{
			std::filesystem::path path = line.substr(key.size());
			while (path != "" and path != path.parent_path() and path.filename() != "io.github.wivrn.wivrn")
				path = path.parent_path();

			return path;
		}
	}

	return "/";
}

static std::string steam_command()
{
	std::string pressure_vessel_filesystems_rw = "$XDG_RUNTIME_DIR/" XRT_IPC_MSG_SOCK_FILENAME;

	// Check if in a flatpak
	if (std::filesystem::exists("/.flatpak-info"))
	{
		std::string app_path = flatpak_app_path().string();
		// /usr and /var are remapped by steam
		if (app_path.starts_with("/usr") or app_path.starts_with("/var"))
			pressure_vessel_filesystems_rw += ":" + app_path;
	}

	std::string command = "PRESSURE_VESSEL_FILESYSTEMS_RW=" + pressure_vessel_filesystems_rw + " %command%";

		if (auto p = active_runtime::manifest_path().string(); p.starts_with("/usr"))
			command = "XR_RUNTIME_JSON=/run/host" + p + " " + command;

	return command;
}

namespace
{
int stdin_pipe_fds[2];
int control_pipe_fds[2];

GMainLoop * main_loop;
const AvahiPoll * poll_api;
bool use_systemd;

guint server_watch;
guint server_kill_watch;
pid_t server_pid;

guint app_watch;
guint app_kill_watch;
pid_t app_pid;

bool quitting_main_loop;

guint listener_watch;

/* TODO: Document FSM
 */
std::optional<active_runtime> runtime_setter;
std::optional<avahi_publisher> publisher;

gboolean headset_connected(gint fd, GIOCondition condition, gpointer user_data);
void stop_listening();

void start_publishing();
void stop_publishing();

void update_fsm();

void start_app()
{
#ifdef WIVRN_USE_SYSTEMD
	app_pid = use_systemd ? start_unit_file() : fork_application();
#else
	app_pid = fork_application();
#endif

	assert(app_watch == 0);
	assert(app_kill_watch == 0);
	if (app_pid)
	{
		app_watch = g_child_watch_add(app_pid, [](pid_t, int status, void *) {
			display_child_status(status, "Application");
			g_source_remove(app_watch);
			if (app_kill_watch)
				g_source_remove(app_kill_watch);

			app_watch = 0;
			app_kill_watch = 0;

			update_fsm(); }, nullptr);
	}
	else
	{
		app_watch = 0;
	}
}

void start_server()
{
	stop_listening();
	stop_publishing();
	server_pid = fork();

	if (server_pid < 0)
	{
		perror("fork");
		g_main_loop_quit(main_loop);
	}
	else if (server_pid == 0)
	{
		// Redirect stdin
		dup2(stdin_pipe_fds[0], 0);
		close(stdin_pipe_fds[0]);
		close(stdin_pipe_fds[1]);

		setenv("LISTEN_PID", std::to_string(getpid()).c_str(), true);

		// In most cases there is no server-side reprojection and
		// there is no need for oversampling.
		setenv("XRT_COMPOSITOR_SCALE_PERCENTAGE", "100", false);

		// Enable mipmaps for distortion
		setenv("XRT_DISTORTION_MIP_LEVELS", "0", false);

		// FIXME: synchronization fails on gfx pipeline
		setenv("XRT_COMPOSITOR_COMPUTE", "1", true);

		setenv("AMD_DEBUG", "lowlatencyenc", false);

		try
		{
			exit(ipc_server_main(0, 0 /*argc, argv*/));
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
		server_watch = g_child_watch_add(server_pid, [](pid_t, int status, void *) {
			display_child_status(status, "Server");
			g_source_remove(server_watch);
			if (server_kill_watch)
				g_source_remove(server_kill_watch);

			server_watch = 0;
			server_kill_watch = 0;

			update_fsm(); }, nullptr);
	}

	runtime_setter.emplace();
}

void kill_app()
{
	kill(-app_pid, SIGTERM);

	// Send SIGKILL after 1s if it is still running
	app_kill_watch = g_timeout_add(1000, [](void *) {
		assert(app_pid > 0);
		kill(-app_pid, SIGKILL);
		return G_SOURCE_REMOVE; }, 0);
}

void kill_server()
{
	// FIXME: server doesn't listen on stdin when used in socket activation mode
	// Write to the server's stdin to make it quit
	char buffer[] = "\n";
	(void)write(stdin_pipe_fds[1], &buffer, strlen(buffer));

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

	listener = std::make_unique<TCPListener>(xrt::drivers::wivrn::default_port);
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
	if (publisher)
		return;

	char protocol_string[17];
	sprintf(protocol_string, "%016lx", xrt::drivers::wivrn::protocol_version);
	std::map<std::string, std::string> TXT = {
	        {"protocol", protocol_string},
	        {"version", xrt::drivers::wivrn::git_version},
	        {"cookie", server_cookie()},
	};
	publisher.emplace(poll_api, hostname(), "_wivrn._tcp", xrt::drivers::wivrn::default_port, TXT);
}

void stop_publishing()
{
	publisher.reset();
}

void update_fsm()
{
	bool server_running = server_watch != 0;
	bool app_running = app_watch != 0;

	if (quitting_main_loop)
	{
		if (server_running)
			kill_server();

		if (app_running)
			kill_app();

		if (not server_running and not app_running)
			g_main_loop_quit(main_loop);
	}
	else
	{
		if (not server_running and app_running)
			kill_app();

		if (not server_running)
		{
			runtime_setter.reset();

			start_listening();
			start_publishing();
		}
	}
}

int sigint(void * sig_nr)
{
	std::cout << strsignal((uintptr_t)sig_nr) << std::endl;

	quitting_main_loop = true;

	update_fsm();

	return G_SOURCE_CONTINUE;
}

gboolean headset_connected(gint fd, GIOCondition condition, gpointer user_data)
{
	assert(server_watch == 0);
	assert(app_watch == 0);

	assert(listener);

	tcp = std::make_unique<xrt::drivers::wivrn::TCP>(listener->accept().first);
	init_cleanup_functions();

	std::cout << "Client connected" << std::endl;

	start_server();
	start_app();

	return true;
}

gboolean control_received(gint fd, GIOCondition condition, gpointer user_data)
{
	uint8_t status;
	if (read(fd, &status, sizeof(status)) == sizeof(status))
	{
		switch (status)
		{
			case 0:
				start_publishing();
				break;
			case 1:
				stop_publishing();
				break;
			default:
				std::cerr << "Unexpected status received: " << (int)status << std::endl;
				break;
		}
	}
	else
	{
		perror("read control pipe");
	}

	return true;
}

} // namespace

int inner_main(int argc, char * argv[], bool show_instructions)
{
	std::cerr << "WiVRn " << xrt::drivers::wivrn::git_version << " starting" << std::endl;
	std::cerr << "For Steam games, set command to " << steam_command() << std::endl;

#ifdef WIVRN_USE_SYSTEMD
	create_listen_socket();
#else
	assert(not use_systemd);
#endif

	u_trace_marker_init();

	// Initialize main loop
	main_loop = g_main_loop_new(nullptr, false);
	auto main_context = g_main_loop_get_context(main_loop);

	// avahi glib integration
	AvahiGLibPoll * glib_poll = avahi_glib_poll_new(main_context, G_PRIORITY_DEFAULT);
	poll_api = avahi_glib_poll_get(glib_poll);

	// Create a pipe to quit monado properly
	if (pipe(stdin_pipe_fds) < 0)
	{
		perror("pipe");
		return EXIT_FAILURE;
	}
	fcntl(stdin_pipe_fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(stdin_pipe_fds[1], F_SETFD, FD_CLOEXEC);

	// Create a pipe to report monado status to the main loop
	if (pipe(control_pipe_fds) < 0)
	{
		perror("pipe");
		return EXIT_FAILURE;
	}
	fcntl(control_pipe_fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(control_pipe_fds[1], F_SETFD, FD_CLOEXEC);
	control_pipe_fd = control_pipe_fds[1];

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

	// Main loop
	g_main_loop_run(main_loop);

	// Cleanup
	runtime_setter.reset();
	stop_publishing();
	stop_listening();
	avahi_glib_poll_free(glib_poll);
	g_main_loop_unref(main_loop);

#ifdef WIVRN_USE_SYSTEMD
	std::error_code ec;
	std::filesystem::remove(socket_path(), ec);
#endif

	return EXIT_SUCCESS;
}

int main(int argc, char * argv[])
{
	CLI::App app;

	std::string config_file;
	app.add_option("-f", config_file, "configuration file")->option_text("FILE")->check(CLI::ExistingFile);
	auto no_instructions = app.add_flag("--no-instructions")->group("");
#ifdef WIVRN_USE_SYSTEMD
	// --application should only be used from wivrn-application unit file
	auto app_flag = app.add_flag("--application")->group("");
	app.add_flag("--systemd", use_systemd, "use systemd to launch user-configured application");
#endif

	CLI11_PARSE(app, argc, argv);

	if (not config_file.empty())
		configuration::set_config_file(config_file);

#ifdef WIVRN_USE_SYSTEMD
	if (*app_flag)
		return exec_application(configuration::read_user_configuration());
#endif
	try
	{
		return inner_main(argc, argv, not *no_instructions);
	}
	catch (std::exception & e)
	{
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}
