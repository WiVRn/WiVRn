#include "avahi_publisher.h"

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>
#include <iostream>

const std::error_category & avahi_error_category()
{
	static struct : std::error_category
	{
		const char * name() const noexcept override
		{
			return "avahi";
		}

		std::string message(int condition) const override
		{
			return avahi_strerror(condition);
		}
	} _avahi_error_category;

	return _avahi_error_category;
}

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
		entry_group = avahi_entry_group_new(client, avahi_entry_group_callback, this);
		if (!entry_group)
		{
			throw std::system_error(avahi_client_errno(client),
			                        avahi_error_category(),
			                        "Cannot create entry group, ensure disable-user-service-publishing is unset in avahi daemon config");
		}
	}

	if (!name)
		name = avahi_strdup("WiVRn");

	std::vector<const char *> txt_array;
	for (const auto & i: txt)
	{
		txt_array.push_back(i.c_str());
	}

	AvahiStringList * txt_list = txt_array.empty() ? nullptr : avahi_string_list_new_from_array(txt_array.data(), txt_array.size());

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
}

void avahi_publisher::avahi_entry_group_callback(
        AvahiEntryGroup * g,
        AvahiEntryGroupState state /**< The new state of the entry group */,
        void * userdata /* The arbitrary user data pointer originally passed to avahi_entry_group_new()*/)
{
	avahi_publisher * self = (avahi_publisher *)userdata;
	// Callback may be called before avahi_entry_group_new returns
	self->entry_group = g;

	switch (state)
	{
		case AVAHI_ENTRY_GROUP_ESTABLISHED:
			std::cout << "Service published: " << self->name << std::endl;
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
			{
				avahi_entry_group_reset(self->entry_group);
			}
			break;

		case AVAHI_CLIENT_S_RUNNING:
			self->create_service(s);
			break;
	}
}

avahi_publisher::avahi_publisher(const AvahiPoll * poll_api, const std::string & name, std::string type, int port, const std::map<std::string, std::string> & txt) :
        name(avahi_strdup(name.c_str())), type(std::move(type)), port(port), poll_api(poll_api)
{
	for (const auto & [key, value]: txt)
	{
		this->txt.push_back(key + "=" + value);
	}

	int error;
	avahi_client = avahi_client_new(
	        poll_api,
	        (AvahiClientFlags)0,
	        &client_callback,
	        this,
	        &error);

	if (!avahi_client)
		throw std::system_error(error, avahi_error_category(), "Cannot create avahi client");
}

avahi_publisher::~avahi_publisher()
{
	avahi_free(name);
	avahi_client_free(avahi_client);
}
/*
AvahiWatch * avahi_publisher::watch_new(int fd,
                                        AvahiWatchEvent event,
                                        void (*callback)(AvahiWatch * w, int fd, AvahiWatchEvent event, void * userdata),
                                        void * userdata)
{
        AvahiWatch * watch = poll_api->watch_new(poll_api, fd, event, callback, userdata);

        return watch;
}

void avahi_publisher::watch_free(AvahiWatch * watch)
{
        poll_api->watch_free(watch);
}*/
