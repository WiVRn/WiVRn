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
#include "spdlog/spdlog.h"

#ifdef __ANDROID__
#include "application.h"
#include <android/native_activity.h>
#include <sys/system_properties.h>

#include "jnipp.h"
#endif

wifi_lock instance;

void wifi_lock::apply()
{
#ifdef __ANDROID__
	spdlog::debug("wifi locks: enabled {} multicast {} low latency {}",
	              enabled,
	              wants_multicast,
	              wants_low_latency);
	jni::object<""> act(application::native_app()->activity->clazz);

	jni::string lock_name("WiVRn");

	static int api_level = jni::klass("android/os/Build$VERSION").field<jni::Int>("SDK_INT");

	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");
	auto wifi_service_id = ctx.klass().field<jni::string>("WIFI_SERVICE");
	auto system_service = ctx.call<jni::object<"java/lang/Object">>("getSystemService", wifi_service_id);
	auto lock = system_service.call<jni::object<"android/net/wifi/WifiManager$MulticastLock">>("createMulticastLock", lock_name);
	lock.call<void>("setReferenceCounted", jni::Bool(false));
	lock.call<void>((enabled and wants_multicast) ? "acquire" : "release");
	if (lock.call<jni::Bool>("isHeld"))
	{
		spdlog::info("MulticastLock acquired");
	}
	else
	{
		spdlog::info("MulticastLock is not acquired");
	}

	auto wifi_lock = system_service.call<jni::object<"android/net/wifi/WifiManager$WifiLock">>(
	        "createWifiLock",
	        jni::Int(api_level >= 29 ? 4 /*WIFI_MODE_FULL_LOW_LATENCY*/ : 3 /*WIFI_MODE_FULL_HIGH_PERF*/),
	        lock_name);
	wifi_lock.call<void>("setReferenceCounted", jni::Bool(false));
	wifi_lock.call<void>((enabled and wants_low_latency) ? "acquire" : "release");
	if (wifi_lock.call<jni::Bool>("isHeld"))
	{
		spdlog::info("WifiLock low latency acquired");
	}
	else
	{
		spdlog::info("WifiLock low latency is not acquired");
	}
#endif
}

void wifi_lock::set_enabled(bool enabled)
{
	std::lock_guard lock(instance.mutex);
	instance.enabled = enabled;
	instance.apply();
}

void wifi_lock::want_multicast(bool enabled)
{
	std::lock_guard lock(instance.mutex);
	instance.wants_multicast += enabled ? 1 : -1;
	instance.apply();
}
void wifi_lock::want_low_latency(bool enabled)
{
	std::lock_guard lock(instance.mutex);
	instance.wants_low_latency += enabled ? 1 : -1;
	instance.apply();
}
