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

#include <atomic>
#include <cassert>
#include <cstdint>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

class libcurl
{
	friend class curl_handle;

public:
	enum class state
	{
		reset,
		transferring,
		cancelling,
		cancelled,
		error,
		done,
	};

private:
	struct transfer
	{
		CURL * curl;
		std::string url;

		state current_state = state::reset;
		CURLcode curl_code = CURLE_OK;
		long response_code = 0;
		int64_t content_length = -1;
		int64_t progress = 0;

		transfer() = default;

		// A pointer to a transfer struct is used as a user pointer for curl callbacks,
		// make sure it never moves
		transfer(const transfer &) = delete;
		transfer(transfer &&) = delete;
		transfer operator=(const transfer &) = delete;
		transfer operator=(transfer &&) = delete;
		virtual ~transfer();

		virtual size_t write(void * data, size_t size) noexcept = 0;
		virtual void finish() noexcept = 0;
		virtual void cancel() noexcept = 0;
	};

	struct transfer_file : transfer
	{
		virtual ~transfer_file();

		std::filesystem::path temporary_path;
		std::filesystem::path final_path;
		std::fstream stream;

		size_t write(void * data, size_t size) noexcept override;
		void finish() noexcept override;
		void cancel() noexcept override;
	};

	struct transfer_buffer : transfer
	{
		virtual ~transfer_buffer();

		std::string buffer;

		size_t write(void * data, size_t size) noexcept override;
		void finish() noexcept override;
		void cancel() noexcept override;
	};

public:
	libcurl();
	libcurl(const libcurl &) = delete;
	libcurl(libcurl &&) = delete;
	libcurl operator=(const libcurl &) = delete;
	libcurl operator=(libcurl &&) = delete;
	~libcurl();

	// Handle returned by the download() functions, it allows cancelling an ongoing transfer
	// by calling cancel() or destroying the handle
	class curl_handle
	{
	private:
		libcurl * lib = nullptr;
		std::shared_ptr<transfer> handle;

		std::filesystem::path path;
		std::string response;

		state current_state = state::reset;
		CURLcode curl_code = CURLE_OK;
		int response_code = 0;
		int64_t content_length = -1;
		int64_t progress = 0;

		friend class libcurl;

		curl_handle(libcurl * lib, std::shared_ptr<transfer> handle, std::filesystem::path path = {}) :
		        lib(lib), handle(handle), path(std::move(path)) {}

	public:
		curl_handle() = default;
		curl_handle(const curl_handle &) = delete;
		curl_handle & operator=(const curl_handle &) = delete;
		curl_handle(curl_handle && other) :
		        lib(other.lib),
		        handle(other.handle),
		        path(std::move(other.path)),
		        response(std::move(other.response)),
		        current_state(other.current_state),
		        curl_code(other.curl_code),
		        response_code(other.response_code),
		        content_length(other.content_length),
		        progress(other.progress)
		{
			other.lib = nullptr;
			other.handle = nullptr;
		}

		curl_handle & operator=(curl_handle && other)
		{
			std::swap(lib, other.lib);
			std::swap(handle, other.handle);
			path = std::move(other.path);
			response = std::move(other.response);
			current_state = other.current_state;
			curl_code = other.curl_code;
			response_code = other.response_code;
			content_length = other.content_length;
			progress = other.progress;

			return *this;
		}

		~curl_handle()
		{
			cancel();
		}

		void reset()
		{
			*this = curl_handle{};
		}

		void sync();
		void cancel();

		state get_state() const
		{
			return current_state;
		}

		CURLcode get_curl_code() const
		{
			return curl_code;
		}

		int get_response_code() const
		{
			return response_code;
		}

		int64_t get_content_length() const
		{
			return content_length;
		}

		int64_t get_progress() const
		{
			return progress;
		}

		const std::string & get_response() const // Only if started by download(url) and state is done
		{
			assert(current_state == state::done);
			return response;
		}

		std::span<const std::byte> get_response_bytes() const // Only if started by download(url) and state is done
		{
			assert(current_state == state::done);
			return {reinterpret_cast<const std::byte *>(response.data()), response.size()};
		}

		const std::filesystem::path & get_path() const // Only if started by download(url, path) and state is done
		{
			assert(current_state == state::done);
			return path;
		}

		const std::string & get_url() const
		{
			return handle->url;
		}
	};

	// Start a download, data is saved on the filesystem
	// Keep the returned handle or the transfer will be immediately cancelled
	curl_handle download(std::string url, std::filesystem::path path);

	// Start a download, data is buffered and can be fetched through the get_response() function of the curl_handle
	// Keep the returned handle or the transfer will be immediately cancelled
	curl_handle download(std::string url);

private:
	std::thread curl_thread;
	void curl_thread_fn();

	std::atomic<bool> quit{false};
	CURLM * multi;

	std::mutex lock; // Locks pending_transfers, pending_cancellations and all struct transfers created by this instance
	std::vector<std::shared_ptr<transfer>> pending_transfers;
	std::vector<std::shared_ptr<transfer>> pending_cancellations;
};
