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

#include "libcurl.h"

#include "utils/mapped_file.h"
#include "utils/named_thread.h"
#include "version.h"
#include <algorithm>
#include <cassert>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/multi.h>
#include <exception>
#include <filesystem>
#include <fstream>
#include <magic_enum.hpp>
#include <mutex>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <system_error>
#include <thread>

libcurl::transfer::~transfer() {}
libcurl::transfer_file::~transfer_file() {}
libcurl::transfer_buffer::~transfer_buffer() {}

libcurl::libcurl()
{
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

	curl_thread = utils::named_thread("curl_thread", &libcurl::curl_thread_fn, this);
}

void libcurl::curl_thread_fn()
{
#ifdef __ANDROID__
	// For Android, read all the certificates at start-up, the file names are not
	// in the correct format for the current verion of OpenSSL.
	// See https://stackoverflow.com/questions/25253823/how-to-make-ssl-peer-verify-work-on-android
	std::string ca_bundle;
	for (const std::filesystem::directory_entry & entry: std::filesystem::directory_iterator{"/system/etc/security/cacerts"})
	{
		if (not entry.is_regular_file())
			continue;

		try
		{
			utils::mapped_file cert{entry.path()};
			ca_bundle += cert;

			// Make sure there is a newline before the next certificate
			if (ca_bundle != "" and ca_bundle.back() != '\n')
				ca_bundle += '\n';
		}
		catch (...)
		{
			// Ignore errors
		}
	}

	curl_blob ca_bundle_info{
	        .data = ca_bundle.data(),
	        .len = ca_bundle.size(),
	        .flags = CURL_BLOB_NOCOPY,
	};
#endif

	std::string user_agent = std::string("WiVRn/") + wivrn::git_version;

	// We can't use auto here because of curl_easy_setopt
	size_t (*write_callback)(void *, size_t, size_t, void *) = [](void * buffer, size_t size, size_t nmemb, void * userp) -> size_t {
		auto & xfer = *reinterpret_cast<libcurl::transfer *>(userp);

		if (xfer.content_length < 0)
		{
			curl_off_t content_length;
			curl_easy_getinfo(xfer.curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
			xfer.content_length = content_length;
		}

		xfer.progress += size * nmemb;
		return xfer.write(buffer, size * nmemb);
	};

	std::vector<std::shared_ptr<transfer>> current_transfers;
	while (not quit)
	{
		try
		{
			// Wait for something to happen (network, new download, cancelled download)
			int numfds;
			if (CURLMcode rc = curl_multi_poll(multi, NULL, 0, 10000, &numfds); rc != CURLM_OK)
				throw std::runtime_error{fmt::format("curl_multi_poll failed: ", curl_multi_strerror(rc))};

			// Only lock the mutex after curl_multi_poll
			std::unique_lock _{lock};

			int still_running;
			if (CURLMcode rc = curl_multi_perform(multi, &still_running); rc != CURLM_OK)
				throw std::runtime_error{fmt::format("curl_multi_perform failed: ", curl_multi_strerror(rc))};

			CURLMsg * msg;
			int msgs_in_queue;
			while ((msg = curl_multi_info_read(multi, &msgs_in_queue)) != nullptr)
			{
				auto iter = std::ranges::find_if(
				        current_transfers,
				        [&](std::shared_ptr<transfer> & x) {
					        return x->curl == msg->easy_handle;
				        });

				if (iter == current_transfers.end())
				{
					spdlog::error("Received {} ({}) for an unknown transfer", magic_enum::enum_name(msg->msg), (int)msg->msg);
					continue;
				}

				auto & xfer = **iter;
				switch (msg->msg)
				{
					case CURLMSG_NONE: // Not used
					case CURLMSG_LAST: // Not used
						break;

					case CURLMSG_DONE: // msg->easy_handle has finished
						xfer.curl_code = msg->data.result;
						curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &xfer.response_code);

						if (msg->data.result == CURLE_OK)
						{
							xfer.current_state = state::done;
							xfer.finish();
						}
						else
						{
							xfer.current_state = state::error;
							xfer.cancel();
						}

						curl_multi_remove_handle(multi, msg->easy_handle);
						curl_easy_cleanup(msg->easy_handle);
						current_transfers.erase(iter);
				}

				break;
			}

			for (std::shared_ptr<transfer> & xfer: pending_transfers)
			{
				// See https://curl.se/libcurl/c/libcurl-tutorial.html and https://curl.se/libcurl/c/curl_easy_setopt.html
				curl_easy_setopt(xfer->curl, CURLOPT_URL, xfer->url.c_str());
				curl_easy_setopt(xfer->curl, CURLOPT_FOLLOWLOCATION, 1); // Older versions of libcurl don't have the constant CURLFOLLOW_ALL
				curl_easy_setopt(xfer->curl, CURLOPT_FILETIME, 1);       // Request remote file time
				curl_easy_setopt(xfer->curl, CURLOPT_FAILONERROR, 1);    // CURLE_HTTP_RETURNED_ERROR on HTTP error
				curl_easy_setopt(xfer->curl, CURLOPT_WRITEFUNCTION, write_callback);
				curl_easy_setopt(xfer->curl, CURLOPT_WRITEDATA, xfer.get());
				curl_easy_setopt(xfer->curl, CURLOPT_USERAGENT, user_agent.c_str());

#ifdef __ANDROID__
				curl_easy_setopt(xfer->curl, CURLOPT_CAINFO_BLOB, &ca_bundle_info);
#endif

				curl_multi_add_handle(multi, xfer->curl);
				current_transfers.push_back(std::move(xfer));
			}

			for (std::shared_ptr<transfer> handle: pending_cancellations)
			{
				auto iter = std::ranges::find(current_transfers, handle);

				// If the transfer cannot be found, it means it has already finished, this is not an error
				if (iter != current_transfers.end())
				{
					auto & xfer = **iter;
					xfer.current_state = state::cancelled;
					xfer.cancel();

					curl_multi_remove_handle(multi, handle->curl);
					curl_easy_cleanup(handle->curl);
					current_transfers.erase(iter);

					spdlog::info("Cancelled transfer from {}", xfer.url);
				}
			}

			pending_transfers.clear();
			pending_cancellations.clear();
		}
		catch (std::exception & e)
		{
			spdlog::error("Exception in curl thread: {}", e.what());
		}
	}

	std::unique_lock _{lock};
	for (auto & xfer: current_transfers)
	{
		xfer->current_state = state::cancelled;
		xfer->cancel();
		curl_multi_remove_handle(multi, xfer->curl);
		curl_easy_cleanup(xfer->curl);
	}

	if (CURLMcode rc = curl_multi_cleanup(multi); rc != CURLM_OK)
		spdlog::warn("curl_multi_cleanup failed: {}", curl_multi_strerror(rc));
}

libcurl::~libcurl()
{
	quit = true;
	curl_multi_wakeup(multi);

	if (curl_thread.joinable())
		curl_thread.join();

	curl_global_cleanup();
}

void libcurl::curl_handle::sync()
{
	if (lib and handle)
	{
		std::unique_lock _{lib->lock};

		current_state = handle->current_state;
		curl_code = handle->curl_code;
		response_code = handle->response_code;
		content_length = handle->content_length;
		progress = handle->progress;

		if (current_state == state::done)
		{
			if (auto h = dynamic_cast<transfer_buffer *>(handle.get()))
			{
				response = h->buffer;
			}
			else if (auto h = dynamic_cast<transfer_file *>(handle.get()))
			{
				path = h->final_path;
			}
			else
			{
				assert(false);
			}
		}
	}
}

void libcurl::curl_handle::cancel()
{
	if (lib and handle)
	{
		{
			std::unique_lock _{lib->lock};
			lib->pending_cancellations.emplace_back(handle);
			if (handle->current_state == state::transferring)
				handle->current_state = state::cancelling;
		}

		curl_multi_wakeup(lib->multi);
	}
}

size_t libcurl::transfer_file::write(void * data, size_t size) noexcept
{
	stream.write(reinterpret_cast<const char *>(data), size);
	if (not stream)
		return CURL_WRITEFUNC_ERROR;
	return size;
}

void libcurl::transfer_file::finish() noexcept
{
	stream.close();
	std::filesystem::rename(temporary_path, final_path);

	curl_off_t remote_timestamp;
	if (curl_easy_getinfo(curl, CURLINFO_FILETIME_T, &remote_timestamp) == CURLE_OK)
	{
		auto time = std::chrono::file_clock::from_sys(std::chrono::system_clock::from_time_t(remote_timestamp));
		std::filesystem::last_write_time(final_path, time);
	}
}

void libcurl::transfer_file::cancel() noexcept
{
	std::error_code ec;
	std::filesystem::remove(temporary_path, ec);
}

libcurl::curl_handle libcurl::download(std::string url, std::filesystem::path path)
{
	auto xfer = std::make_shared<transfer_file>();
	xfer->curl = curl_easy_init();
	xfer->url = url;
	xfer->final_path = path;
	xfer->temporary_path = path.native() + ".partial";
	xfer->stream.open(xfer->temporary_path, std::ios::out);
	xfer->current_state = state::transferring;

	std::unique_lock _{lock};
	pending_transfers.push_back(xfer);

	// The curl thread needs to be woken up to pick up the newly added handle without waiting for the timeout
	curl_multi_wakeup(multi);

	return {this, xfer, path};
}

size_t libcurl::transfer_buffer::write(void * data, size_t size) noexcept
{
	buffer.append(reinterpret_cast<const char *>(data), size);
	return size;
}

void libcurl::transfer_buffer::finish() noexcept
{
}

void libcurl::transfer_buffer::cancel() noexcept
{
}

libcurl::curl_handle libcurl::download(std::string url)
{
	auto xfer = std::make_shared<transfer_buffer>();
	xfer->curl = curl_easy_init();
	xfer->url = url;
	xfer->current_state = state::transferring;

	std::unique_lock _{lock};
	pending_transfers.push_back(xfer);

	// The curl thread needs to be woken up to pick up the newly added handle without waiting for the timeout
	curl_multi_wakeup(multi);

	return {this, xfer};
}
