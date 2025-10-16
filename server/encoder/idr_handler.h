/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "wivrn_packets.h"

#include <cstdint>
#include <mutex>
#include <variant>

namespace wivrn
{

class idr_handler
{
public:
	virtual ~idr_handler();
	virtual void on_feedback(const from_headset::feedback &) = 0;
	virtual void reset() = 0;
	virtual bool should_skip(uint64_t frame_id) = 0;
};

// handler for unknown P-frames
// any lost frame triggers an I-frame
// skip frames until th I frame is received
class default_idr_handler : public idr_handler
{
	std::mutex mutex;
	struct need_idr
	{};
	struct wait_idr_feedback
	{
		uint64_t idr_id;
	};
	struct idr_received
	{};
	struct running
	{
		uint64_t first_p;
	};
	std::variant<need_idr, wait_idr_feedback, idr_received, running> state;

public:
	enum class frame_type
	{
		i,
		p,
	};

	void on_feedback(const from_headset::feedback &) override;
	void reset() override;
	bool should_skip(uint64_t frame_id) override;
	frame_type get_type(uint64_t frame_index);
};
} // namespace wivrn
