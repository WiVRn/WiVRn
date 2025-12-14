/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace utils
{
enum class future_status
{
	ready,
	timeout
};

template <typename Result, typename Progress>
struct async_token;

template <typename Result, typename Progress>
struct future;

template <typename Result, typename Progress, typename F, typename... Args>
future<Result, Progress> async(F && f, Args &&... args);

template <typename Result, typename Progress>
struct future
{
	friend struct async_token<Result, Progress>;

	template <typename F, typename... Args>
	friend future<Result, Progress> async(F && f, Args &&... args);

	struct state
	{
		std::mutex lock;
		std::condition_variable cv;

		std::optional<Result> result;
		Progress progress;
		std::exception_ptr exception;
		std::atomic<bool> cancelled = false;

		std::thread thread;

		~state()
		{
			if (thread.joinable())
			{
				if (thread.get_id() == std::this_thread::get_id())
					thread.detach();
				else
					thread.join();
			}
		}
	};

	std::shared_ptr<state> shared_state;

public:
	bool valid() const
	{
		return (bool)shared_state;
	}

	future_status poll() const
	{
		assert(valid());

		std::unique_lock _{shared_state->lock};

		if (shared_state->result || shared_state->exception)
			return future_status::ready;
		else
			return future_status::timeout;
	}

	// future_status wait_until(std::chrono::steady_clock::time_point deadline) const
	// {
	// 	assert(valid());
	//
	// 	std::unique_lock _{shared_state->lock};
	// 	auto now = std::chrono::steady_clock::now();
	// 	while(now < deadline)
	// 	{
	// 		if (shared_state->result || shared_state->exception)
	// 			return future_status::ready;
	//
	// 		shared_state->cv.wait_until(_, deadline);
	// 	}
	// 	return future_status::timeout;
	// }
	//
	// future_status wait_for(std::chrono::steady_clock::duration duration) const
	// {
	// 	return wait_until(std::chrono::steady_clock::now() + duration);
	// }

	Result get()
	{
		assert(valid());

		std::unique_lock _{shared_state->lock};
		shared_state->cv.wait(_, [&] {
			return shared_state->result || shared_state->exception;
		});

		if (shared_state->thread.joinable())
			shared_state->thread.join();

		if (shared_state->result)
			return std::move(*shared_state->result);

		std::rethrow_exception(shared_state->exception);
	}

	Progress get_progress()
	{
		assert(valid());
		std::unique_lock _{shared_state->lock};
		return shared_state->progress;
	}

	void cancel()
	{
		if (!valid())
			return;

		shared_state->cancelled = true;

		shared_state.reset();
	}

	void reset()
	{
		shared_state.reset();
	}
};

template <typename Result, typename Progress>
struct async_token
{
	template <typename R, typename P, typename F, typename... Args>
	friend future<R, P> async(F && f, Args &&... args);

	using state = typename future<Result, Progress>::state;
	std::shared_ptr<state> shared_state;

	async_token(std::shared_ptr<state> shared_state) :
	        shared_state(shared_state)
	{
	}

	async_token(const async_token &) = default;
	async_token(async_token &&) = default;
	async_token & operator=(const async_token &) = default;
	async_token & operator=(async_token &&) = default;

	void set_result(Result r)
	{
		assert(shared_state);
		std::unique_lock _{shared_state->lock};
		shared_state->result = std::move(r);
		shared_state->cv.notify_all();
	}

	void set_exception(std::exception_ptr e)
	{
		assert(shared_state);
		std::unique_lock _{shared_state->lock};
		shared_state->exception = std::move(e);
		shared_state->cv.notify_all();
	}

public:
	void set_progress(Progress p)
	{
		assert(shared_state);
		std::unique_lock _{shared_state->lock};
		shared_state->progress = std::move(p);
		shared_state->cv.notify_all();
	}

	bool is_cancelled()
	{
		return shared_state->cancelled;
	}
};

template <typename Result, typename Progress, typename F, typename... Args>
future<Result, Progress> async(F && f, Args &&... args)
{
	future<Result, Progress> fut;

	fut.shared_state = std::make_shared<typename future<Result, Progress>::state>();

	fut.shared_state->thread = std::thread(
	        [f = std::move(f)](async_token<Result, Progress> token, Args &&... args) {
		        try
		        {
			        token.set_result(std::invoke(f, token, std::forward<Args>(args)...));
		        }
		        catch (...)
		        {
			        token.set_exception(std::current_exception());
		        }
	        },
	        async_token<Result, Progress>(fut.shared_state),
	        std::forward<Args>(args)...);

	return fut;
}
} // namespace utils
