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

#pragma once
#include <memory>

#ifdef __ANDROID__
#include "android/jnipp.h"
#include <mutex>
#endif

class wifi_lock : public std::enable_shared_from_this<wifi_lock>
{
#ifdef __ANDROID__
	std::mutex mutex;
	jni::object<"android/net/wifi/WifiManager$MulticastLock"> multicast_;
	jni::object<"android/net/wifi/WifiManager$WifiLock"> wifi_;
	wifi_lock(decltype(multicast_), decltype(wifi_));

	void print_wifi();
	void acquire_wifi();
	void release_wifi();
	void print_multicast();
	void acquire_multicast();
	void release_multicast();

public:
	static std::shared_ptr<wifi_lock> make_wifi_lock(jobject activity);
#endif

public:
	using multicast = std::shared_ptr<void>;
	using wifi = std::shared_ptr<void>;

	multicast get_multicast_lock();
	wifi get_wifi_lock();
};
