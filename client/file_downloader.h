/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "asset.h"
#include <atomic>
#include <curl/curl.h>
#include <curl/multi.h>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <thread>

class file_downloader
{
	std::thread curl_thread;

	std::atomic<bool> quit{false};
	CURLM * multi;

#ifdef __ANDROID__
	asset ca_bundle;
#endif

	struct transfer
	{
		std::function<void(std::span<const std::byte>)> write;
		std::function<void(CURLcode)> done;
	};

	std::map<CURL *, transfer> transfers;

public:
	file_downloader();
	file_downloader(const file_downloader &) = delete;
	file_downloader(file_downloader &&) = delete;
	file_downloader operator=(const file_downloader &) = delete;
	file_downloader operator=(file_downloader &&) = delete;
	~file_downloader();

	void download(const std::string & url, std::function<void(std::span<const std::byte>)> write, std::function<void(CURLcode)> done);
};
