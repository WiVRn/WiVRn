#include "wivrn_discover.h"
#include "mdns.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <string>
#include <sys/ioctl.h>
#include <variant>
#include <vector>

using namespace std::chrono;

namespace
{
std::string ip_address_to_string(const in_addr & address)
{
	char buffer[128];
	if (inet_ntop(AF_INET, &address, buffer, sizeof(buffer)))
		return buffer;

	return "";
}

std::string ip_address_to_string(const in6_addr & address)
{
	char buffer[128];
	if (inet_ntop(AF_INET6, &address, buffer, sizeof(buffer)))
		return buffer;

	return "";
}

std::string ip_address_to_string(const sockaddr * address)
{
	if (address->sa_family == AF_INET)
	{
		return ip_address_to_string(((struct sockaddr_in *)address)->sin_addr);
	}
	else if (address->sa_family == AF_INET6)
	{
		return ip_address_to_string(((struct sockaddr_in6 *)address)->sin6_addr);
	}

	return "";
}
} // namespace

static bool operator==(const in_addr & a, const in_addr & b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

static bool operator==(const in6_addr & a, const in6_addr & b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}

class dnssd_cache
{
public:
	using ptr = std::string;
	struct srv
	{
		std::string hostname;
		int port;
		bool operator==(const srv &) const noexcept = default;
	};
	using a = in_addr;
	using aaaa = in6_addr;

private:
	struct cache_entry
	{
		steady_clock::time_point timeout;
		std::string name;
		std::variant<ptr, srv, a, aaaa> record;

		bool operator==(const cache_entry & other) const noexcept
		{
			return name == other.name && record == other.record;
		}
	};

	std::vector<cache_entry> cache;
	std::vector<std::tuple<steady_clock::time_point, mdns_record_type, std::string>> last_queries;

	// pollfds[0] is used for inter-thread communication, the others are for mDNS
	std::vector<pollfd> pollfds;
	int itc_fd;

	void open_client_sockets(int port = 0)
	{
		close_client_sockets();
		std::vector<int> sockets;

		ifaddrs * addresses;
		if (getifaddrs(&addresses) < 0)
		{
			spdlog::error("Cannot get network interfaces: {}", strerror(errno));
			return;
		}

		const int required_flags = IFF_UP | IFF_MULTICAST;
		const int forbidden_flags = IFF_LOOPBACK;

		// std::vector<std::string> interfaces_ipv4;
		// std::vector<std::string> interfaces_ipv6;

		for (ifaddrs * i = addresses; i; i = i->ifa_next)
		{
			if (i->ifa_addr == nullptr)
				continue;

			if ((i->ifa_flags & required_flags) != required_flags)
				continue;

			if ((i->ifa_flags & forbidden_flags) != 0)
				continue;

			if (i->ifa_addr->sa_family == AF_INET6)
			{
				// if (std::any_of(interfaces_ipv6.begin(), interfaces_ipv6.end(), [&](const auto & j) { return j == i->ifa_name; }))
				// continue;

				struct sockaddr_in6 * saddr = (struct sockaddr_in6 *)i->ifa_addr;
				// Ignore link-local addresses
				if (saddr->sin6_scope_id)
				{
					spdlog::trace("Ignoring link-local address {}", ip_address_to_string(i->ifa_addr));
					continue;
				}
				static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
				static const unsigned char localhost_mapped[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
				if (memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
				    memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16))
				{
					saddr->sin6_port = htons(port);
					int sock = mdns_socket_open_ipv6(saddr);
					if (sock >= 0)
					{
						sockets.push_back(sock);

						spdlog::info("Local IPv6 address: {}", ip_address_to_string(i->ifa_addr));
						// interfaces_ipv6.push_back(i->ifa_name);
					}
					else
					{
						spdlog::info("Cannot open socket bound to {}: {}", ip_address_to_string(i->ifa_addr), strerror(errno));
					}
				}
			}
			else if (i->ifa_addr->sa_family == AF_INET)
			{
				// if (std::any_of(interfaces_ipv4.begin(), interfaces_ipv4.end(), [&](const auto & j) { return j == i->ifa_name; }))
				// continue;

				struct sockaddr_in * saddr = (struct sockaddr_in *)i->ifa_addr;
				if (saddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK))
				{
					saddr->sin_port = htons(port);
					int sock = mdns_socket_open_ipv4(saddr);
					if (sock >= 0)
					{
						sockets.push_back(sock);

						spdlog::info("Local IPv4 address: {}", ip_address_to_string(i->ifa_addr));
						// interfaces_ipv4.push_back(i->ifa_name);
					}
					else
					{
						spdlog::info("Cannot open socket bound to {}: {}", ip_address_to_string(i->ifa_addr), strerror(errno));
					}
				}
			}
		}

		freeifaddrs(addresses);

		pollfds.reserve(sockets.size() + 1);

		int fds[2];
		pipe(fds);
		itc_fd = fds[1];
		pollfds.push_back({.fd = fds[0], .events = POLLIN});

		for (int socket: sockets)
			pollfds.push_back(pollfd{.fd = socket, .events = POLLIN});
	}

	void close_client_sockets()
	{
		for (pollfd socket: pollfds)
			mdns_socket_close(socket.fd);

		pollfds.clear();
		itc_fd = -1;
	}

	void log_entry(const cache_entry & entry)
	{
		std::string type;
		std::string record;
		if (std::holds_alternative<ptr>(entry.record))
		{
			type = "PTR";
			record = std::get<ptr>(entry.record);
		}
		else if (std::holds_alternative<srv>(entry.record))
		{
			type = "SRV";
			auto [addr, port] = std::get<srv>(entry.record);
			record = addr + ":" + std::to_string(port);
		}
		else if (std::holds_alternative<a>(entry.record))
		{
			type = "A";
			auto addr = std::get<a>(entry.record);
			record = ip_address_to_string(addr);
		}
		else if (std::holds_alternative<aaaa>(entry.record))
		{
			type = "AAAA";
			auto addr = std::get<aaaa>(entry.record);
			record = ip_address_to_string(addr);
		}

		auto ttl = entry.timeout - steady_clock::now();
		spdlog::info("{:40} {:4} {:40} TTL {}", entry.name, type, record, duration_cast<seconds>(ttl).count());
	}

	std::string_view record_type_name(mdns_record_type record_type)
	{
		switch (record_type)
		{
			case MDNS_RECORDTYPE_A:
				return "A";
			case MDNS_RECORDTYPE_AAAA:
				return "AAAA";
			case MDNS_RECORDTYPE_ANY:
				return "ANY";
			case MDNS_RECORDTYPE_PTR:
				return "PTR";
			case MDNS_RECORDTYPE_SRV:
				return "SRV";
			case MDNS_RECORDTYPE_TXT:
				return "TXT";
			default:
				return "UNKNOWN";
		}
	}

	void log_query(mdns_record_type record_type, std::string_view service_name)
	{
		spdlog::info("Sending query for {}, type {}", service_name, record_type_name(record_type));
	}

	void update(mdns_string_t entry, std::variant<ptr, srv, a, aaaa> record, int ttl)
	{
		auto now = steady_clock::now();

		cache_entry e =
		        {
		                .timeout = now + seconds(ttl),
		                .name = std::string(entry.str, entry.length),
		                .record = std::move(record),
		        };

		// log_entry(e);

		if (ttl == 0)
		{
			std::erase_if(cache, [&e](const cache_entry & i) {
				return i == e;
			});
		}
		else
		{
			auto it = std::find(cache.begin(), cache.end(), e);
			if (it == cache.end())
				cache.push_back(std::move(e));
			else
				it->timeout = e.timeout;
		}
	}

	static int query_callback(int sock, const sockaddr * from, size_t addrlen, mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype, uint16_t rclass, uint32_t ttl, const void * data, size_t size, size_t name_offset, size_t name_length, size_t record_offset, size_t record_length, void * user_data)
	{
		dnssd_cache * self = (dnssd_cache *)user_data;

		char namebuffer[256];
		char entrybuffer[256];
		sockaddr_in ipv4_buffer;
		sockaddr_in6 ipv6_buffer;

		mdns_string_t entrystr = mdns_string_extract(data, size, &name_offset, entrybuffer, sizeof(entrybuffer));

		switch (rtype)
		{
			case MDNS_RECORDTYPE_PTR: {
				auto ptr_record = mdns_record_parse_ptr(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
				self->update(entrystr, std::string{ptr_record.str, ptr_record.length}, ttl);
				break;
			}

			case MDNS_RECORDTYPE_SRV: {
				auto srv_record = mdns_record_parse_srv(data, size, record_offset, record_length, namebuffer, sizeof(namebuffer));
				self->update(entrystr, srv{{srv_record.name.str, srv_record.name.length}, srv_record.port}, ttl);
				break;
			}

			case MDNS_RECORDTYPE_A:
				self->update(entrystr, mdns_record_parse_a(data, size, record_offset, record_length, &ipv4_buffer)->sin_addr, ttl);
				break;

			case MDNS_RECORDTYPE_AAAA:
				self->update(entrystr, mdns_record_parse_aaaa(data, size, record_offset, record_length, &ipv6_buffer)->sin6_addr, ttl);
				break;

			default:
				break;
		}

		return 0;
	}

public:
	void send_query(mdns_record_type record, std::string service_name)
	{
		auto now = steady_clock::now();

		std::erase_if(last_queries, [now](const auto & q) {
			return now - std::get<steady_clock::time_point>(q) > 5s;
		});

		for (const auto & [i, j, k]: last_queries)
		{
			if (j == record && k == service_name)
			{
				// spdlog::info("Query for {} {} already sent {}ms ago, skipping", service_name, record_type_name(record), duration_cast<milliseconds>(now - i).count());
				return;
			}
		}

		log_query(record, service_name);
		std::array<uint8_t, 2048> buffer;
		for (size_t i = 1; i < pollfds.size(); i++)
		{
			if (mdns_query_send(pollfds[i].fd, record, service_name.data(), service_name.size(), buffer.data(), buffer.size(), 0))
			{
				spdlog::error("Failed to send DNS-DS discovery for {}, record type {}: {}", service_name, record_type_name(record), strerror(errno));
			}
		}

		last_queries.emplace_back(now, record, std::move(service_name));
	}

	void poll_response(milliseconds ms)
	{
		std::array<uint8_t, 2048> buffer;
		int timeout = ms < 0ms ? -1 : std::min<int64_t>(std::numeric_limits<int>::max(), ms.count());

		poll(pollfds.data(), pollfds.size(), timeout);

		if (pollfds[0].revents & POLLIN)
		{
			::read(pollfds[0].fd, buffer.data(), buffer.size());
		}

		for (size_t i = 1; i < pollfds.size(); i++)
		{
			if (pollfds[i].revents & POLLIN)
			{
				mdns_query_recv(pollfds[i].fd, buffer.data(), buffer.size(), query_callback, this, 0);
			}
		}
	}

	void stop_polling()
	{
		char c = 0;
		write(itc_fd, &c, 1);
	}

	template <typename T>
	std::vector<std::pair<T, steady_clock::time_point>> read(const std::string & name)
	{
		std::vector<std::pair<T, steady_clock::time_point>> entries;
		auto now = steady_clock::now();

		std::erase_if(cache, [now](const cache_entry & i) {
			return i.timeout < now;
		});

		for (const auto & i: cache)
		{
			if (i.name == name && std::holds_alternative<T>(i.record))
				entries.emplace_back(std::get<T>(i.record), i.timeout);
		}

		return entries;
	}

	dnssd_cache()
	{
		open_client_sockets(5353);
	}

	dnssd_cache(const dnssd_cache &) = delete;

	~dnssd_cache()
	{
		close_client_sockets();
	}
};

void wivrn_discover::discover(std::string service_name)
{
	cache->send_query(MDNS_RECORDTYPE_PTR, service_name);

	milliseconds poll_timeout = poll_max_time;
	std::vector<service> services_staging;

	while (!quit)
	{
		poll_timeout = std::clamp(poll_timeout, poll_min_time, poll_max_time);
		cache->poll_response(poll_timeout);

		poll_timeout = poll_max_time;

		auto now = steady_clock::now();
		for (auto [ptr, ttl]: cache->read<dnssd_cache::ptr>(service_name))
		{
			bool srv_found = false;
			auto srv_min_ttl = milliseconds::max();

			service s;
			// Remove the suffix
			if (ptr.ends_with("." + service_name))
				s.name = ptr.substr(0, ptr.size() - service_name.size() - 1);
			else
				s.name = ptr;

			for (auto [srv, ttl]: cache->read<dnssd_cache::srv>(ptr))
			{
				srv_found = true;
				srv_min_ttl = std::min(duration_cast<milliseconds>(ttl - now), srv_min_ttl);

				// Remove the suffix
				if (srv.hostname.ends_with(".local."))
					s.hostname = srv.hostname.substr(0, srv.hostname.size() - 7);
				else if (srv.hostname.ends_with("."))
					s.hostname = srv.hostname.substr(0, srv.hostname.size() - 1);
				else
					s.hostname = srv.hostname;

				s.port = srv.port;
				s.ttl = ttl;

				bool address_found = false;
				auto address_min_ttl = milliseconds::max();
				for (auto [a, ttl]: cache->read<dnssd_cache::a>(srv.hostname))
				{
					address_found = true;
					address_min_ttl = std::min(duration_cast<milliseconds>(ttl - now), address_min_ttl);
					// spdlog::info("PTR={}, SRV={}:{}, A={}, TTL={}ms", ptr, srv.hostname, srv.port, ip_address_to_string(a), ttl.count());
					s.addresses.push_back(a);
				}

				for (auto [aaaa, ttl]: cache->read<dnssd_cache::aaaa>(srv.hostname))
				{
					address_found = true;
					address_min_ttl = std::min(duration_cast<milliseconds>(ttl - now), address_min_ttl);
					// spdlog::info("PTR={}, SRV={}:{}, AAAA={}, TTL={}ms", ptr, srv.hostname, srv.port, ip_address_to_string(aaaa), ttl.count());
					s.addresses.push_back(aaaa);
				}

				if (!address_found || address_min_ttl < 5s)
				{
					cache->send_query(MDNS_RECORDTYPE_ANY, srv.hostname);
				}
				else // address_found && address_min_ttl >= 5s
				{
					poll_timeout = std::min(poll_timeout, address_min_ttl - 4s);
				}

				services_staging.push_back(s);
			}

			if (!srv_found || srv_min_ttl < 10s)
			{
				cache->send_query(MDNS_RECORDTYPE_SRV, ptr);
			}
			else // srv_found && srv_min_ttl >= 10s
			{
				poll_timeout = std::min(poll_timeout, srv_min_ttl - 9s);
			}
		}

		std::lock_guard lock(mutex);
		std::swap(services, services_staging);
		services_staging.clear();
	}
}

wivrn_discover::wivrn_discover(std::string service_name) :
        cache(std::make_unique<dnssd_cache>()), dnssd_thread(&wivrn_discover::discover, this, service_name)
{
}

wivrn_discover::~wivrn_discover()
{
	quit = true;
	cache->stop_polling();
	dnssd_thread.join();
}

std::vector<wivrn_discover::service> wivrn_discover::get_services()
{
	std::lock_guard lock(mutex);

	auto now = steady_clock::now();

	std::erase_if(services, [now](const service & i) {
		return i.ttl < now;
	});

	return services;
}

// g++ wivrn_discover.cpp -I .. -g $(pkg-config spdlog --cflags --libs) -std=c++20 -DTEST && ./a.out
#ifdef TEST
int main()
{
	wivrn_discover wd("_wivrn._tcp.local.");

	while (true)
	{
		auto now = steady_clock::now();
		auto services = wd.get_services();

		spdlog::info("{} service(s) found", services.size());
		for (auto & i: services)
		{
			spdlog::info("    {} at {}:{}, expires in {} s", i.name, i.hostname, i.port, duration_cast<seconds>(i.ttl - now).count());
			for (auto & j: i.addresses)
			{
				std::visit([](const auto & addr) {
					spdlog::info("        {}", ip_address_to_string(addr));
				},
				           j);
			}
		}

		pollfd pfd{.fd = 0, .events = POLLIN};
		poll(&pfd, 1, 1000);

		if (pfd.revents & POLLIN)
			break;
	}
}
#endif
