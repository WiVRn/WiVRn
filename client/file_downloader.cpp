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

#include "file_downloader.h"

#include "utils/named_thread.h"
#include <cassert>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <exception>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>

file_downloader::file_downloader()
{
#ifdef __ANDROID__
	ca_bundle = asset{"ca-bundle.crt"};
#endif

	if (CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT); rc != CURLE_OK)
	{
		throw std::runtime_error{fmt::format("Cannot initialize libcurl: {}", curl_easy_strerror(rc))};

		curl_version_info_data * data = curl_version_info(CURLVERSION_NOW);
		if (not(data->features & CURL_VERSION_THREADSAFE))
			spdlog::warn("libcurl features do not include CURL_VERSION_THREADSAFE");
	}

	multi = curl_multi_init();
	if (not multi)
	{
		spdlog::error("curl_multi_init failed");
		return;
	}

	curl_thread = utils::named_thread("curl_thread", [&]() {
		while (not quit)
		{
			try
			{
				int numfds;
				if (CURLMcode rc = curl_multi_poll(multi, NULL, 0, 10000, &numfds); rc != CURLM_OK)
				{
					throw std::runtime_error{fmt::format("curl_multi_poll failed: ", curl_multi_strerror(rc))};
				}

				int still_running;
				if (CURLMcode rc = curl_multi_perform(multi, &still_running); rc != CURLM_OK)
				{
					throw std::runtime_error{fmt::format("curl_multi_perform failed: ", curl_multi_strerror(rc))};
				}

				while (true)
				{
					int msgs_in_queue;
					CURLMsg * msg = curl_multi_info_read(multi, &msgs_in_queue);
					if (not msg)
						break;

					switch (msg->msg)
					{
						case CURLMSG_NONE: // Not used
						case CURLMSG_LAST: // Not used
							break;

						case CURLMSG_DONE: // msg->easy_handle has finished
						{
							auto & xfer = transfers.at(msg->easy_handle);
							if (xfer.done)
								xfer.done(msg->data.result);

							curl_multi_remove_handle(multi, msg->easy_handle);
							curl_easy_cleanup(msg->easy_handle);
							transfers.erase(msg->easy_handle);
						}

						break;
					}
				}
			}
			catch (std::exception & e)
			{
				spdlog::error("Exception in curl thread: {}", e.what());
			}
		}
	});
}

file_downloader::~file_downloader()
{
	quit = true;
	curl_multi_wakeup(multi);

	if (curl_thread.joinable())
		curl_thread.join();

	for (auto & [c, xfer]: transfers)
	{
		curl_multi_remove_handle(multi, c);
		curl_easy_cleanup(c);
	}

	if (CURLMcode rc = curl_multi_cleanup(multi); rc != CURLM_OK)
	{
		spdlog::warn("curl_multi_cleanup failed: {}", curl_multi_strerror(rc));
	}

	curl_global_cleanup();
}

void file_downloader::download(const std::string & url, std::function<void(std::span<const std::byte>)> write, std::function<void(CURLcode)> done)
{
	// We can't use auto here because of curl_easy_setopt
	size_t (*write_data)(void *, size_t, size_t, void *) = [](void * buffer, size_t size, size_t nmemb, void * userp) -> size_t {
		auto & xfer = *reinterpret_cast<file_downloader::transfer *>(userp);

		if (xfer.write)
			xfer.write(std::span<const std::byte>(reinterpret_cast<const std::byte *>(buffer), size * nmemb));

		return size * nmemb;
	};

	auto & [curl, xfer] = *transfers.emplace(
	                                        curl_easy_init(),
	                                        transfer{
	                                                .write = std::move(write),
	                                                .done = std::move(done),
	                                        })
	                               .first;

	// See https://curl.se/libcurl/c/libcurl-tutorial.html and https://curl.se/libcurl/c/curl_easy_setopt.html
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, CURLFOLLOW_ALL);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&xfer);

#ifdef __ANDROID__
	curl_blob ca_bundle_info{
	        .data = (void *)ca_bundle.data(),
	        .len = ca_bundle.size(),
	        .flags = CURL_BLOB_NOCOPY,
	};

	spdlog::info("Using CA info blob size {}", ca_bundle_info.len);
	curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca_bundle_info);
#endif

	curl_multi_add_handle(multi, curl);

	// The curl thread needs to be woken up to pick up the newly added handle without waiting for the timeout
	curl_multi_wakeup(multi);
}
