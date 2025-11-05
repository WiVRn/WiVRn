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

#include "idr_handler.h"

#include "util/u_logging.h"
#include "utils/overloaded.h"

namespace wivrn
{
idr_handler::~idr_handler() = default;

void default_idr_handler::on_feedback(const from_headset::feedback & f)
{
	std::unique_lock lock(mutex);
	std::visit(utils::overloaded{
	                   [](need_idr) {},
	                   [](idr_received) {},
	                   [this, &f](wait_idr_feedback s) {
		                   if (f.sent_to_decoder and f.frame_index == s.idr_id)
		                   {
			                   U_LOG_D("IDR frame received");
			                   state = idr_received{};
		                   }
	                   },
	                   [this, &f](running r) {
		                   if (not f.sent_to_decoder and f.frame_index >= r.first_p)
			                   state = need_idr{};
	                   },
	           },
	           state);
}

void default_idr_handler::reset()
{
	std::unique_lock lock(mutex);
	U_LOG_D("IDR handler reset");
	state = need_idr{};
}

bool default_idr_handler::should_skip(uint64_t frame_id)
{
	std::unique_lock lock(mutex);
	return std::visit(utils::overloaded{
	                          [this, frame_id](wait_idr_feedback w) {
		                          if (frame_id > w.idr_id + 100)
		                          {
			                          state = need_idr{};
			                          return false;
		                          }
		                          return true;
	                          },
	                          [](auto) {
		                          return false;
	                          },
	                  },
	                  state);
}

default_idr_handler::frame_type default_idr_handler::get_type(uint64_t frame_index)
{
	std::unique_lock lock(mutex);
	return std::visit(utils::overloaded{
	                          [this, frame_index](need_idr) {
		                          U_LOG_D("IDR frame needed");
		                          state = wait_idr_feedback{frame_index};
		                          return frame_type::i;
	                          },
	                          [this, frame_index](idr_received) {
		                          state = running{frame_index};
		                          return frame_type::p;
	                          },
	                          [](auto) {
		                          return frame_type::p;
	                          },
	                  },
	                  state);
}
} // namespace wivrn
