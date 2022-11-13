#include "hostname.h"

#include <systemd/sd-bus.h>

std::string hostname()
{
	sd_bus * bus;
	if (sd_bus_default_system(&bus) < 0)
		return "";

	for (auto property: {"PrettyHostname", "StaticHostname", "Hostname"})
	{
		char * hostname = nullptr;
		sd_bus_error error = SD_BUS_ERROR_NULL;

		if (sd_bus_get_property_string(bus, "org.freedesktop.hostname1", "/org/freedesktop/hostname1", "org.freedesktop.hostname1", property, &error, &hostname) < 0)
		{
			sd_bus_error_free(&error);
			continue;
		}

		sd_bus_error_free(&error);
		if (hostname && strcmp(hostname, ""))
		{
			std::string s = hostname;
			free(hostname);
			sd_bus_unref(bus);
			return s;
		}
	}

	sd_bus_unref(bus);

	return "Unknown";
}
