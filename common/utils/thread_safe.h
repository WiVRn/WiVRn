/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include <condition_variable>
#include <mutex>

template <typename T>
class thread_safe_notifyable;

template <typename T, typename Mutex = std::mutex>
class locked
{
	friend class thread_safe_notifyable<T>;

	T & value;
	std::unique_lock<Mutex> lock;

public:
	locked(T & value, Mutex & mutex) :
	        value(value),
	        lock(mutex)
	{}

	T * operator->()
	{
		return &value;
	}

	const T * operator->() const
	{
		return &value;
	}

	T & operator*()
	{
		return value;
	}

	const T & operator*() const
	{
		return value;
	}
};

template <typename T, typename Mutex = std::mutex>
class thread_safe
{
	T value;
	Mutex mutex;

public:
	template <typename... Args>
	thread_safe(Args &&... args) :
	        value(std::forward<Args>(args)...)
	{}

	locked<T, Mutex> lock()
	{
		return locked<T, Mutex>(value, mutex);
	}

	T & get_unsafe()
	{
		return value;
	}

	const T & get_unsafe() const
	{
		return value;
	}
};

template <typename T>
class locked_notifiable
{
	friend class thread_safe_notifyable<T>;

	T & value;
	std::unique_lock<std::mutex> lock;
	std::condition_variable & cv;

public:
	locked_notifiable(T & value, std::mutex & mutex, std::condition_variable & cv) :
	        value(value),
	        lock(mutex),
	        cv(cv)
	{}

	T * operator->()
	{
		return &value;
	}

	const T * operator->() const
	{
		return &value;
	}

	T & operator*()
	{
		return value;
	}

	const T & operator*() const
	{
		return value;
	}

	void notify_one() noexcept
	{
		cv.notify_one();
	}

	void notify_all() noexcept
	{
		cv.notify_all();
	}

	void wait()
	{
		cv.wait(lock);
	}

	template <class Predicate>
	void wait(Predicate && pred)
	{
		cv.wait(lock, std::forward<Predicate>(pred));
	}

	template <class Rep, class Period>
	std::cv_status wait_for(const std::chrono::duration<Rep, Period> & rel_time)
	{
		return cv.wait_for(lock, rel_time);
	}

	template <class Rep, class Period, class Predicate>
	bool wait_for(const std::chrono::duration<Rep, Period> & rel_time, Predicate pred)
	{
		return cv.wait_for(lock, rel_time, pred);
	}

	template <class Clock, class Duration>
	std::cv_status wait_until(const std::chrono::time_point<Clock, Duration> & abs_time)
	{
		return cv.wait_until(lock, abs_time);
	}

	template <class Clock, class Duration, class Predicate>
	bool wait_until(const std::chrono::time_point<Clock, Duration> & abs_time, Predicate pred)
	{
		return cv.wait_until(lock, abs_time, pred);
	}
};

template <typename T>
class thread_safe_notifyable : public thread_safe<T, std::mutex>
{
	T value;
	std::mutex mutex;
	std::condition_variable cv;

public:
	template <typename... Args>
	thread_safe_notifyable(Args &&... args) :
	        thread_safe<T, std::mutex>(std::forward<Args>(args)...)
	{}

	locked_notifiable<T> lock()
	{
		return locked_notifiable<T>(value, mutex, cv);
	}
};
