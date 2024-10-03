#include "hostname.h"
#include <limits.h>
#include <unistd.h>

#include "util/u_logging.h"

#include <gio/gio.h>

static std::string _hostname()
{
	GError * error = NULL;
	GDBusConnection * con = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

	if (!con)
	{
		U_LOG_W("Failed to connect to system bus: %s", error->message);
	}
	else
	{
		for (auto property: {"PrettyHostname", "StaticHostname", "Hostname"})
		{
			GVariant * result = g_dbus_connection_call_sync(con,
			                                                "org.freedesktop.hostname1",
			                                                "/org/freedesktop/hostname1",
			                                                "org.freedesktop.DBus.Properties",
			                                                "Get",
			                                                g_variant_new("(ss)", "org.freedesktop.hostname1", property),
			                                                G_VARIANT_TYPE("(v)"),
			                                                G_DBUS_CALL_FLAGS_NONE,
			                                                -1,
			                                                NULL,
			                                                &error);

			if (error)
				continue;

			GVariant * property_value;
			g_variant_get(result, "(v)", &property_value);
			const char * hostname = g_variant_get_string(property_value, NULL);

			if (hostname && strcmp(hostname, ""))
			{
				std::string s = hostname;
				g_variant_unref(property_value);
				g_variant_unref(result);
				g_object_unref(con);
				return s;
			}

			g_variant_unref(property_value);
			g_variant_unref(result);
		}

		g_object_unref(con);
	}

	char buf[HOST_NAME_MAX];
	int code = gethostname(buf, sizeof(buf));
	if (code == 0)
		return buf;

	U_LOG_W("Failed to get hostname");
	return "no-hostname";
}

std::string wivrn::hostname()
{
	// Accessing hostname in child process fails with glib
	static std::string result = _hostname();
	return result;
}
