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

#include "stream.h"

void scenes::stream::read_actions()
{
	from_headset::inputs inputs;
	application::poll_actions();

	for (const auto & [id, action, action_type]: input_actions)
	{
		switch (action_type)
		{
			case XR_ACTION_TYPE_BOOLEAN_INPUT: {
				bool value;
				if (application::read_action(action, value))
					inputs.values.push_back({id, (float)value});
			}
			break;

			case XR_ACTION_TYPE_FLOAT_INPUT: {
				float value;
				if (application::read_action(action, value))
					inputs.values.push_back({id, value});
			}
			break;

			case XR_ACTION_TYPE_VECTOR2F_INPUT: {
				XrVector2f value;
				if (application::read_action(action, value))
				{
					inputs.values.push_back({id, value.x});
					inputs.values.push_back({(device_id)((int)id + 1), value.y});
				}
			}
			break;

			case XR_ACTION_TYPE_POSE_INPUT:
			default:
				break;
		}
	}
	try
	{
		network_session->send_stream(inputs);
	}
	catch (std::exception & e)
	{
		spdlog::warn("Exception while sending inputs packet: {}", e.what());
	}
}

void scenes::stream::operator()(to_headset::haptics && haptics)
{
	XrAction action;
	XrPath subpath;

	if (haptics.id == device_id::LEFT_CONTROLLER_HAPTIC)
	{
		action = haptics_actions[0].first;
		subpath = haptics_actions[0].second;
	}
	else if (haptics.id == device_id::RIGHT_CONTROLLER_HAPTIC)
	{
		action = haptics_actions[1].first;
		subpath = haptics_actions[1].second;
	}
	else
		return;

	if (haptics.amplitude > 0)
		application::haptic_start(action, subpath, haptics.duration.count(), haptics.frequency, haptics.amplitude);
	else
		application::haptic_stop(action, subpath);
}
