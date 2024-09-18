#pragma once

#include "utils/named_thread.h"
#include <chrono>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <variant>
#include <vector>

class dnssd_cache;

class wivrn_discover
{
public:
	struct service
	{
		std::string name;
		std::string hostname;
		int port;
		bool tcp_only = false;

		std::vector<std::variant<in_addr, in6_addr>> addresses;
		std::map<std::string, std::string> txt;
		std::chrono::steady_clock::time_point ttl;
	};

	static inline const std::chrono::milliseconds poll_min_time{500};
	static inline const std::chrono::milliseconds poll_max_time{10000};
	static inline const std::chrono::milliseconds discover_period{5000};

private:
	std::unique_ptr<dnssd_cache> cache;

	std::thread dnssd_thread;
	std::mutex mutex;
	std::vector<service> services;
	std::atomic<bool> quit = false;

	void discover(std::string service_name);

public:
	wivrn_discover(std::string service_name = "_wivrn._tcp.local.");
	wivrn_discover(const wivrn_discover &) = delete;
	wivrn_discover(wivrn_discover &&) = delete;
	wivrn_discover operator=(const wivrn_discover &) = delete;
	wivrn_discover operator=(wivrn_discover &&) = delete;
	~wivrn_discover();

	std::vector<service> get_services();
};
