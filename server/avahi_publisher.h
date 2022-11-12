#pragma once

#include <avahi-common/watch.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <string>

class avahi_publisher
{
	AvahiEntryGroup *entry_group{};
	char *name{};
	std::string type;
	int port;

	AvahiSimplePoll *avahi_poll{};
	AvahiClient *avahi_client{};

	void
	alt_name();

	void
	create_service(AvahiClient *client);

	static void
	avahi_entry_group_callback(
	    AvahiEntryGroup *g,
	    AvahiEntryGroupState state /**< The new state of the entry group */,
	    void *userdata /* The arbitrary user data pointer originally passed to avahi_entry_group_new()*/);

	static void
	client_callback(AvahiClient *s,
	                AvahiClientState state /**< The new state of the client */,
	                void *userdata /**< The user data that was passed to avahi_client_new() */);

public:
	avahi_publisher(const char *name, std::string type, int port);

	~avahi_publisher();

	AvahiWatch *
	watch_new(int fd,
	          AvahiWatchEvent event,
	          void (*callback)(AvahiWatch *w, int fd, AvahiWatchEvent event, void *userdata),
	          void *userdata);

	void
	watch_free(AvahiWatch *watch);

	bool
	iterate(int sleep_time = -1);
};
