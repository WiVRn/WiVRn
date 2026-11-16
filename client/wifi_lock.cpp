/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2024  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "wifi_lock.h"
#include <exception>
#include <spdlog/spdlog.h>

#ifdef __ANDROID__
#include "application.h"
#include <android/native_activity.h>
#include <sys/system_properties.h>

#include "android/jnipp.h"

wifi_lock::wifi_lock(decltype(multicast_) m, decltype(wifi_) w) :
        multicast_(std::move(m)), wifi_(std::move(w)) {}

std::shared_ptr<wifi_lock> wifi_lock::make_wifi_lock(jobject activity)
{
	jni::object<""> act(application::native_app()->activity->clazz);

	jni::string lock_name("WiVRn");

	static int api_level = jni::klass("android/os/Build$VERSION").field<jni::Int>("SDK_INT");

	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");
	auto wifi_service_id = ctx.klass().field<jni::string>("WIFI_SERVICE");
	auto system_service = ctx.call<jni::object<"java/lang/Object">>("getSystemService", wifi_service_id);
	return std::shared_ptr<wifi_lock>(new wifi_lock(
	        system_service.call<jni::object<"android/net/wifi/WifiManager$MulticastLock">>("createMulticastLock", lock_name),
	        system_service.call<jni::object<"android/net/wifi/WifiManager$WifiLock">>(
	                "createWifiLock",
	                jni::Int(api_level >= 29 ? 4 /*WIFI_MODE_FULL_LOW_LATENCY*/ : 3 /*WIFI_MODE_FULL_HIGH_PERF*/),
	                lock_name)));
}

void wifi_lock::print_wifi()
{
	if (wifi_.call<jni::Bool>("isHeld"))
		spdlog::info("WifiLock low latency acquired");
	else
		spdlog::info("WifiLock low latency released");
}

void wifi_lock::print_multicast()
{
	if (multicast_.call<jni::Bool>("isHeld"))
		spdlog::info("MulticastLock acquired");
	else
		spdlog::info("MulticastLock released");
}

std::shared_ptr<void> wifi_lock::get_wifi_lock()
{
	std::lock_guard lock(mutex);
	if (auto l = wifi_weak.lock())
		return l;

	wifi_.call<void>("acquire");
	print_wifi();

	// The shared pointer has a separate reference counter: destructor only calls a method
	auto res = std::shared_ptr<void>(
	        this,
	        [self = shared_from_this()](void *) {
	std::lock_guard lock(self->mutex);
	self->wifi_.call<void>("release");
	self->print_wifi(); });
	wifi_weak = res;
	return res;
}
std::shared_ptr<void> wifi_lock::get_multicast_lock()
{
	std::lock_guard lock(mutex);
	if (auto l = multicast_weak.lock())
		return l;

	multicast_.call<void>("acquire");
	print_multicast();

	// The shared pointer has a separate reference counter: destructor only calls a method
	auto res = std::shared_ptr<void>(
	        this,
	        [self = shared_from_this()](void *) {
	std::lock_guard lock(self->mutex);
	self->multicast_.call<void>("release");
	self->print_multicast(); });
	multicast_weak = res;
	return res;
}

#else
#include <algorithm>
#include <gio/gio.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;

constexpr const char * service = "fi.w1.wpa_supplicant1";
constexpr const char * interface_properties = "org.freedesktop.DBus.Properties";

void throw_and_free(GError * error) noexcept(false)
{
	std::string msg = error->message;
	g_error_free(error);
	throw std::runtime_error(msg);
}

variant call(const std::string & path, const char * interface, const char * method, GVariant * arguments = nullptr) noexcept(false)
{
	GError * error = nullptr;

	std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> bus(g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error), g_object_unref);
	if (error)
		throw_and_free(error);

	GVariant * reply = g_dbus_connection_call_sync(bus.get(), service, path.c_str(), interface, method, arguments, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
	if (error)
		throw_and_free(error);

	return variant(reply, g_variant_unref);
}

variant get_property(const std::string & path, const char * interface, const char * name) noexcept(false)
{
	auto reply = call(path, interface_properties, "Get", g_variant_new("(ss)", interface, name));
	GVariant * value;
	g_variant_get(reply.get(), "(v)", &value);
	return variant(value, g_variant_unref);
}

std::vector<std::string> find_networks() noexcept(false)
{
	std::vector<std::string> networks;

	auto interfaces = get_property("/fi/w1/wpa_supplicant1", service, "Interfaces");
	gchar ** paths = g_variant_dup_objv(interfaces.get(), nullptr);

	for (gchar ** interface = paths; *interface; ++interface)
	{
		try
		{
			auto network = get_property(*interface, "fi.w1.wpa_supplicant1.Interface", "CurrentNetwork");
			std::string path = g_variant_get_string(network.get(), nullptr);
			if (path != "/")
				networks.push_back(path);
		}
		catch (const std::exception & e)
		{
			spdlog::warn("skipping {}: {}\n", *interface, e.what());
		}
	}

	g_strfreev(paths);
	return networks;
}

std::string read_bgscan(const std::string & network) noexcept(false)
{
	auto properties = get_property(network, "fi.w1.wpa_supplicant1.Network", "Properties");

	const char * value = "";
	g_variant_lookup(properties.get(), "bgscan", "&s", &value);

	std::string text = value;
	if (text.size() >= 2 and text.front() == '"' and text.back() == '"')
		return text.substr(1, text.size() - 2);

	return text;
}

void write_bgscan(const std::string & network, const std::string & value) noexcept(false)
{
	GVariantBuilder dictionary;
	g_variant_builder_init(&dictionary, G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(&dictionary, "{sv}", "bgscan", g_variant_new_string(value.c_str()));

	call(network, interface_properties, "Set", g_variant_new("(ssv)", "fi.w1.wpa_supplicant1.Network", "Properties", g_variant_builder_end(&dictionary)));
}
} // namespace

std::shared_ptr<void> wifi_lock::get_wifi_lock()
{
	std::lock_guard lock(mutex);
	if (auto l = wifi_weak.lock())
		return l;

	std::vector<std::string> discovered_networks;
	try
	{
		discovered_networks = find_networks();
	}
	catch (std::exception & e)
	{
		spdlog::warn("failed to find wifi networks: {}", e.what());
	}

	for (const auto & network: discovered_networks)
	{
		bool not_saved = std::none_of(saved_networks.begin(), saved_networks.end(), [&network](const auto & item) {
			return item.path == network;
		});

		if (not_saved)
		{
			try
			{
				saved_networks.emplace_back(network, read_bgscan(network));
			}
			catch (std::exception & e)
			{
				spdlog::warn("failed to read wifi bgscan for {}: {}", network, e.what());
				continue;
			}
		}

		try
		{
			write_bgscan(network, "");
		}
		catch (const std::exception & e)
		{
			spdlog::warn("failed to write wifi bgscan for {}: {}", network, e.what());
		}
	}

	auto res = std::shared_ptr<void>(
	        this,
	        [self = shared_from_this()](void *) {
		        std::lock_guard lock(self->mutex);
		        if (self->wifi_weak.lock())
			        return;
		        for (const auto & network: self->saved_networks)
		        {
			        try
			        {
				        write_bgscan(network.path, network.value);
			        }
			        catch (const std::exception & e)
			        {
				        spdlog::warn("failed to restore bgscan status for network: {}", e.what());
			        }
		        }
		        self->saved_networks.clear();
	        });
	wifi_weak = res;
	return res;
}

std::shared_ptr<void> wifi_lock::get_multicast_lock()
{
	return nullptr;
}

#endif
