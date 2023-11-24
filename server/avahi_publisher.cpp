#include "avahi_publisher.h"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>
#include <iostream>
#include <string.h>

void avahi_publisher::alt_name()
{
	char * new_name = avahi_alternative_service_name(name);
	avahi_free(name);
	name = new_name;
}

void avahi_publisher::create_service(AvahiClient * client)
{
	if (!entry_group)
	{
		entry_group = avahi_entry_group_new(client, avahi_entry_group_callback, nullptr);
		if (!entry_group)
		{
			throw std::runtime_error(
			        std::string(
			                "Cannot create entry group, you may need to remove disable-user-service-publishing from your avahi daemon config: ") +
			        avahi_strerror(avahi_client_errno(client)));
		}
	}
	avahi_entry_group_reset(entry_group);

	if (!name)
		name = avahi_strdup("WiVRn");

	std::vector<const char *> txt_array;
	for(const auto& i: txt)
	{
		txt_array.push_back(i.c_str());
	}

	AvahiStringList* txt_list = txt_array.empty() ? nullptr : avahi_string_list_new_from_array(txt_array.data(), txt_array.size());

	int ret;
	do
	{

		ret = avahi_entry_group_add_service_strlst(entry_group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags)0, name, type.c_str(), nullptr, nullptr, port, txt_list);

		if (ret == AVAHI_ERR_COLLISION)
		{
			alt_name();
		}
	} while (ret == AVAHI_ERR_COLLISION);

	avahi_string_list_free(txt_list);

	if (ret < 0)
	{
		std::cerr << "Cannot add service: " << avahi_strerror(ret) << std::endl;
	}

	ret = avahi_entry_group_commit(entry_group);
	if (ret < 0)
	{
		std::cerr << "Cannot commit entry group: " << avahi_strerror(ret) << std::endl;
	}
	else
	{
		std::cout << "Service published: " << name << std::endl;
	}
}

void avahi_publisher::avahi_entry_group_callback(
        AvahiEntryGroup * g,
        AvahiEntryGroupState state /**< The new state of the entry group */,
        void * userdata /* The arbitrary user data pointer originally passed to avahi_entry_group_new()*/)
{
	avahi_publisher * self = (avahi_publisher *)userdata;

	switch (state)
	{
		case AVAHI_ENTRY_GROUP_ESTABLISHED:
		case AVAHI_ENTRY_GROUP_FAILURE:
		case AVAHI_ENTRY_GROUP_REGISTERING:
		case AVAHI_ENTRY_GROUP_UNCOMMITED:
			break;
		case AVAHI_ENTRY_GROUP_COLLISION:
			self->alt_name();
			self->create_service(avahi_entry_group_get_client(g));

			break;
	}
}

void avahi_publisher::client_callback(AvahiClient * s,
                                      AvahiClientState state /**< The new state of the client */,
                                      void * userdata /**< The user data that was passed to avahi_client_new() */)
{
	avahi_publisher * self = (avahi_publisher *)userdata;
	switch (state)
	{
		case AVAHI_CLIENT_CONNECTING:
		case AVAHI_CLIENT_FAILURE:
			break;

		case AVAHI_CLIENT_S_COLLISION:
		case AVAHI_CLIENT_S_REGISTERING:
			if (self->entry_group)
				avahi_entry_group_reset(self->entry_group);
			break;

		case AVAHI_CLIENT_S_RUNNING:
			self->create_service(s);
			break;
	}
}

avahi_publisher::avahi_publisher(const char * name, std::string type, int port, const std::map<std::string, std::string>& txt) :
        name(avahi_strdup(name)), type(std::move(type)), port(port)
{
	for(const auto& [key, value]: txt)
	{
		this->txt.push_back(key + "=" + value);
	}

	avahi_poll = avahi_simple_poll_new();

	int error;
	avahi_client =
	        avahi_client_new(avahi_simple_poll_get(avahi_poll), (AvahiClientFlags)0, &client_callback, this, &error);

	if (!avahi_client)
		throw std::runtime_error(std::string("Cannot create avahi client: ") + avahi_strerror(error));
}

avahi_publisher::~avahi_publisher()
{
	avahi_free(name);
	avahi_client_free(avahi_client);
	avahi_simple_poll_free(avahi_poll);
}

AvahiWatch * avahi_publisher::watch_new(int fd,
                                        AvahiWatchEvent event,
                                        void (*callback)(AvahiWatch * w, int fd, AvahiWatchEvent event, void * userdata),
                                        void * userdata)
{
	AvahiWatch * watch = avahi_simple_poll_get(avahi_poll)
	                             ->watch_new(avahi_simple_poll_get(avahi_poll), fd, event, callback, userdata);

	return watch;
}

void avahi_publisher::watch_free(AvahiWatch * watch)
{
	avahi_simple_poll_get(avahi_poll)->watch_free(watch);
}

bool avahi_publisher::iterate(int sleep_time)
{
	int r = avahi_simple_poll_iterate(avahi_poll, sleep_time);

	if (r < 0 && errno != EINTR)
		throw std::runtime_error(std::string("avahi_simple_poll_iterate: ") + strerror(errno));

	return r == 0;
}
