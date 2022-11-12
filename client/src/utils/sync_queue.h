/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <cassert>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>

namespace utils
{
class sync_queue_closed : public std::exception
{
public:
	const char * what() const noexcept override
	{
		return "sync_queue_closed";
	}
};

template <typename T>
class sync_queue
{
	std::deque<T> queue;
	std::condition_variable cv;
	std::mutex mutex;
	bool closed = false;

public:
	void push(T && item)
	{
		std::lock_guard lock(mutex);

		queue.push_back(std::move(item));
		cv.notify_one();
	}

	void push(const T & item)
	{
		std::lock_guard lock(mutex);

		queue.push_back(item);
		cv.notify_one();
	}

	template <typename Pred>
	std::optional<T> pop_if(Pred && pred)
	{
		std::unique_lock lock(mutex);

		cv.wait(lock, [&]() { return !queue.empty() || closed; });

		if (closed)
			throw sync_queue_closed{};

		assert(!queue.empty());

		if (pred(queue.front()))
		{
			T item = std::move(queue.front());
			queue.pop_front();

			return item;
		}

		return {};
	}

	T pop()
	{
		std::unique_lock lock(mutex);

		cv.wait(lock, [&]() { return !queue.empty() || closed; });

		if (closed)
			throw sync_queue_closed{};

		assert(!queue.empty());
		T item = std::move(queue.front());
		queue.pop_front();

		return item;
	}

	template <typename Pred>
	void drop_until(Pred && pred)
	{
		std::unique_lock lock(mutex);

		while (!queue.empty() && !pred(queue.front()))
			queue.pop_front();
	}

	T & peek()
	{
		std::unique_lock lock(mutex);

		cv.wait(lock, [&]() { return !queue.empty() || closed; });

		if (closed)
			throw sync_queue_closed{};

		assert(!queue.empty());
		return queue.front();
	}

	void close()
	{
		std::lock_guard lock(mutex);
		closed = true;
		cv.notify_all();
	}
};
} // namespace utils
