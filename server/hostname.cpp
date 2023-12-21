#include "hostname.h"
#include <limits.h>
#include <unistd.h>

#include "util/u_logging.h"
#include "wivrn_config.h"

#ifdef WIVRN_USE_SYSTEMD
#include <systemd/sd-bus.h>
#endif

std::string hostname()
{

#ifdef WIVRN_USE_SYSTEMD
	sd_bus * bus;
	if (sd_bus_default_system(&bus) < 0)
	{
		U_LOG_W("Failed to connect to system bus");
	}
	else
	{
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
	}
#endif

	char buf[HOST_NAME_MAX];
	int code = gethostname(buf, sizeof(buf));
	if (code == 0)
		return buf;

	U_LOG_W("Failed to get hostname");
	return "no-hostname";
}
