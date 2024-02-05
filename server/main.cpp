// Copyright 2022, Collabora, Ltd.
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  Main file for WiVRn Monado service.
 * @author
 * @author
 * @ingroup ipc
 */

#include "util/u_trace_marker.h"

#include "driver/configuration.h"
#include "wivrn_ipc.h"
#include "wivrn_packets.h"
#include "wivrn_sockets.h"
#include <iostream>
#include <memory>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pidfd.h"
#include "version.h"
#include "wivrn_config.h"

#include "avahi_publisher.h"
#include "hostname.h"
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

void avahi_set_bool_callback(AvahiWatch * w, int fd, AvahiWatchEvent event, void * userdata)
{
	bool * flag = (bool *)userdata;
	*flag = true;
}

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

pid_t start_application()
{
	auto config = configuration::read_user_configuration();

	if (!config.application.empty())
	{
		pid_t application_pid = fork();
		if (application_pid < 0)
		{
			throw std::system_error(errno, std::system_category(), "fork");
		}

		if (application_pid == 0)
		{
			// Start a new process group so that all processes started by the
			// application can be signaled
			setpgrp();

			std::vector<char *> argv;
			argv.reserve(config.application.size() + 1);
			for (auto & arg: config.application)
				argv.push_back(arg.data());
			argv.push_back(nullptr);

			execvp(config.application.front().c_str(), argv.data());

			perror("Cannot start application");
			exit(EXIT_FAILURE);
		}

		return application_pid;
	}

	return 0;
}

void waitpid_verbose(pid_t pid, const std::string & name)
{
	int wstatus = 0;
	waitpid(pid, &wstatus, 0);

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

int inner_main(int argc, char * argv[])
{
	std::cerr << "WiVRn " << xrt::drivers::wivrn::git_version << " starting" << std::endl;
#ifdef WIVRN_USE_SYSTEMD
	create_listen_socket();
#endif

	u_trace_marker_init();

	sigset_t sigint_mask;
	sigemptyset(&sigint_mask);
	sigaddset(&sigint_mask, SIGINT);
	sigaddset(&sigint_mask, SIGTERM);

	sigprocmask(SIG_BLOCK, &sigint_mask, nullptr);
	int sigint_fd = signalfd(-1, &sigint_mask, 0);

	// Create a pipe to quit monado properly
	int pipe_fds[2];
	if (pipe(pipe_fds) < 0)
	{
		perror("pipe");
		return EXIT_FAILURE;
	}
	fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
	fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC);

	char protocol_string[17];
	sprintf(protocol_string, "%016lx", xrt::drivers::wivrn::protocol_version);

	std::map<std::string, std::string> TXT;
	TXT["protocol"] = protocol_string;
	TXT["version"] = xrt::drivers::wivrn::git_version;
	TXT["cookie"] = server_cookie();

	bool quit = false;
	while (!quit)
	{
		try
		{
			avahi_publisher publisher(hostname().c_str(), "_wivrn._tcp", default_port, TXT);

			TCPListener listener(default_port);
			bool client_connected = false;
			bool sigint_received = false;

			AvahiWatch * watch_listener = publisher.watch_new(listener.get_fd(), AVAHI_WATCH_IN, &avahi_set_bool_callback, &client_connected);
			AvahiWatch * watch_sigint = publisher.watch_new(sigint_fd, AVAHI_WATCH_IN, &avahi_set_bool_callback, &sigint_received);

			while (publisher.iterate() && !client_connected && !sigint_received)
				;

			publisher.watch_free(watch_listener);
			publisher.watch_free(watch_sigint);

			if (client_connected)
			{
				tcp = std::make_unique<TCP>(listener.accept().first);

				init_cleanup_functions();
			}

			if (sigint_received)
				break;
		}
		catch (std::exception & e)
		{
			std::cerr << e.what() << std::endl;
			return EXIT_FAILURE;
		}

		pid_t client_pid = start_application();

		pid_t server_pid = fork();

		if (server_pid < 0)
		{
			perror("fork");
			return EXIT_FAILURE;
		}
		if (server_pid == 0)
		{
			// Unmask SIGTERM, keep SIGINT masked
			sigset_t sigint_mask;
			sigemptyset(&sigint_mask);
			sigaddset(&sigint_mask, SIGTERM);
			sigprocmask(SIG_UNBLOCK, &sigint_mask, nullptr);
			close(sigint_fd);

			// Redirect stdin
			dup2(pipe_fds[0], 0);
			close(pipe_fds[0]);
			close(pipe_fds[1]);

			setenv("LISTEN_PID", std::to_string(getpid()).c_str(), true);

			//FIXME: synchronization fails on gfx pipeline
			setenv("XRT_COMPOSITOR_COMPUTE", "1", true);

			try
			{
				return ipc_server_main(argc, argv);
			}
			catch (std::exception & e)
			{
				std::cerr << e.what() << std::endl;
				return EXIT_FAILURE;
			}
		}
		else
		{
			std::cerr << "Server started, PID " << server_pid << std::endl;

			tcp.reset();

			int server_fd = pidfd_open(server_pid, 0);
			int client_fd = client_pid > 0 ? pidfd_open(client_pid, 0) : -1;

			pollfd fds[3]{};
			fds[0].fd = sigint_fd;
			fds[0].events = POLLIN;
			fds[1].fd = server_fd;
			fds[1].events = POLLIN;
			fds[2].fd = client_fd;
			fds[2].events = POLLIN;

			bool server_running = true;
			bool client_running = client_pid > 0;

			while (!quit && server_running)
			{
				poll(fds, std::size(fds), -1);

				if (fds[0].revents & POLLIN)
				{
					// SIGINT/SIGTERM received
					quit = true;
				}

				if (fds[1].revents & POLLIN)
				{
					// Server exited
					waitpid_verbose(server_pid, "Server");

					server_running = false;
					fds[1].fd = -1;
				}

				if (fds[2].revents & POLLIN)
				{
					// Client application exited
					waitpid_verbose(server_pid, "Client");

					client_running = false;
					fds[2].fd = -1;
				}
			}

			// Quit the server and the client

			if (client_running)
				kill(-client_pid, SIGTERM);

			if (server_running)
			{
				// FIXME: server doesn't listen on stdin when used in socket activation mode
				// Write to the server's stdin to make it quit
				char buffer[] = "\n";
				(void)write(pipe_fds[1], &buffer, strlen(buffer));
			}

			// Wait until both the server and the client exit
			auto now = std::chrono::steady_clock::now();
			while (server_running or client_running)
			{
				poll(fds, std::size(fds), 100);

				if (fds[1].revents & POLLIN)
				{
					// Server exited
					waitpid_verbose(server_pid, "Server");

					server_running = false;
					fds[1].fd = -1;
				}

				if (fds[2].revents & POLLIN)
				{
					// Client application exited
					waitpid_verbose(server_pid, "Client");

					client_running = false;
					fds[2].fd = -1;
				}

				if (std::chrono::steady_clock::now() - now > std::chrono::seconds(1))
				{
					// Send SIGTERM if the server takes more than 1s to quit
					if (server_running)
					{
						pidfd_send_signal(server_fd, SIGTERM, nullptr, 0);
					}

					// Send SIGKILL if the client takes more than 1s to quit
					if (client_running)
					{
						kill(-client_pid, SIGKILL);
					}
				}
			}

			close(server_fd);
			if (client_fd > 0)
				close(client_fd);

			run_cleanup_functions();
		}
	}
	return EXIT_SUCCESS;
}

int main(int argc, char * argv[])
{
	try
	{
		return inner_main(argc, argv);
	}
	catch (std::exception & e)
	{
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}
}
