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

#include "wivrn_ipc.h"
#include "wivrn_packets.h"
#include "wivrn_sockets.h"
#include <iostream>
#include <memory>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C"
{
#include <sys/pidfd.h>
}

#include "avahi_publisher.h"
#include "hostname.h"
#include <shared/ipc_protocol.h>
#include <systemd/sd-daemon.h>
#include <util/u_file.h>

// Insert the on load constructor to init trace marker.
U_TRACE_TARGET_SETUP(U_TRACE_WHICH_SERVICE)

extern "C"
{
	int
	ipc_server_main(int argc, char * argv[]);

	int
	oxr_sdl2_hack_create(void ** out_hack)
	{
		return 0;
	}

	int
	oxr_sdl2_hack_start(void * hack, struct xrt_instance * xinst, struct xrt_system_devices * xsysd)
	{
		return 0;
	}

	int
	oxr_sdl2_hack_stop(void ** hack_ptr)
	{
		return 0;
	}
}

using namespace xrt::drivers::wivrn;

std::unique_ptr<TCP> tcp;

void avahi_set_bool_callback(AvahiWatch * w, int fd, AvahiWatchEvent event, void * userdata)
{
	bool * flag = (bool *)userdata;
	*flag = true;
}

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

#ifdef XRT_HAVE_LIBBSD
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
#endif

	if (ret < 0)
	{
		std::cerr << "Could not bind socket to path " << sock_file << ": " << strerror(errno) << ". Is the service running already?" << std::endl;
		if (errno == EADDRINUSE)
		{
			std::cerr << "If wivrn is not running, delete " << sock_file << " before starting a new instance" << std::endl;
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


	assert(fd == SD_LISTEN_FDS_START);

	setenv("LISTEN_FDS", "1", true);

	return fd;
}

int main(int argc, char * argv[])
{
	create_listen_socket();

	u_trace_marker_init();

	sigset_t sigint_mask;
	sigemptyset(&sigint_mask);
	sigaddset(&sigint_mask, SIGINT);
	sigaddset(&sigint_mask, SIGTERM);

	sigprocmask(SIG_BLOCK, &sigint_mask, nullptr);
	int sigint_fd = signalfd(-1, &sigint_mask, 0);

	bool quit = false;
	while (!quit)
	{
		{
			avahi_publisher publisher(hostname().c_str(), "_wivrn._tcp", control_port);

			TCPListener listener(control_port);
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

		pid_t child = fork();

		if (child < 0)
		{
			perror("fork");
			return 1;
		}
		if (child == 0)
		{
			sigprocmask(SIG_UNBLOCK, &sigint_mask, nullptr);
			close(sigint_fd);

			setenv("LISTEN_PID", std::to_string(getpid()).c_str(), true);

			return ipc_server_main(argc, argv);
		}
		else
		{
			std::cerr << "Server started, PID " << child << std::endl;

			tcp.reset();

			int child_fd = pidfd_open(child, 0);

			pollfd fds[2]{};
			fds[0].fd = child_fd;
			fds[0].events = POLLIN;
			fds[1].fd = sigint_fd;
			fds[1].events = POLLIN;

			while (true)
			{
				poll(fds, std::size(fds), -1);

				if (fds[1].revents & POLLIN)
				{
					// SIGINT/SIGTERM received
					quit = true;
				}

				if (fds[0].revents & POLLIN)
				{
					// Child exited
					int wstatus = 0;
					waitpid(child, &wstatus, 0);

					std::cerr << "Server exited, exit status " << WEXITSTATUS(wstatus) << std::endl;
					if (WIFSIGNALED(wstatus))
						std::cerr << "Received signal " << sigabbrev_np(WTERMSIG(wstatus)) << " ("
						          << strsignal(WTERMSIG(wstatus)) << ")"
						          << (WCOREDUMP(wstatus) ? ", core dumped" : "") << std::endl;

					break;
				}
			}

			close(child_fd);

			run_cleanup_functions();
		}
	}
}
