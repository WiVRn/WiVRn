#pragma once

#include <avahi-client/publish.h>
#include <avahi-common/watch.h>
#include <map>
#include <string>
#include <system_error>
#include <vector>

const std::error_category & avahi_error_category();

class avahi_publisher
{
	AvahiEntryGroup * entry_group{};
	char * name{};
	std::string type;
	int port;
	std::vector<std::string> txt;

	const AvahiPoll * poll_api{};
	AvahiClient * avahi_client{};

	void alt_name();

	void create_service(AvahiClient * client);

	static void avahi_entry_group_callback(
	        AvahiEntryGroup * g,
	        AvahiEntryGroupState state /**< The new state of the entry group */,
	        void * userdata /* The arbitrary user data pointer originally passed to avahi_entry_group_new()*/);

	static void client_callback(AvahiClient * s,
	                            AvahiClientState state /**< The new state of the client */,
	                            void * userdata /**< The user data that was passed to avahi_client_new() */);

public:
	avahi_publisher(const AvahiPoll * poll_api, const std::string & name, std::string type, int port, const std::map<std::string, std::string> & txt = {});
	avahi_publisher(const avahi_publisher &) = delete;
	avahi_publisher & operator=(const avahi_publisher &) = delete;
	~avahi_publisher();
};
